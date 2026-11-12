// The timeline controller: deleting several clips is one undo step, failed actions raise toasts, snapping
// toggles with S, scrolling is clamped, and track rows follow the project.
#include "TimelineFixture.h"
#include "TimelineModel.h"

#include <QSignalSpy>
#include <QVariantMap>

using namespace Qt::StringLiterals;
using mikserstudio::ui::TimelineController;
using mikserstudio::ui::TimelineModel;
using mikserstudio::ui::test::TimelineFixture;

namespace {

QStringList trackIds(const TimelineController& timeline)
{
    QStringList ids;
    for (const QVariant& track : timeline.tracks())
        ids.append(track.toMap().value(u"id"_s).toString());
    return ids;
}

QVariantMap trackEntry(const TimelineController& timeline, const QString& id)
{
    for (const QVariant& track : timeline.tracks()) {
        if (track.toMap().value(u"id"_s) == id)
            return track.toMap();
    }
    return {};
}

QVariant clipData(const TimelineModel& model, const QString& clipId, int role)
{
    return model.data(model.index(model.rowOfClip(clipId)), role);
}

}  // namespace

TEST_CASE("Deleting several selected clips is one undo step", "[timeline-controller]")
{
    TimelineFixture f;
    f.addClip(u"clip-a"_s, u"V1"_s, 0, 0, 90);
    f.addClip(u"clip-b"_s, u"V1"_s, 100, 0, 90);
    f.addClip(u"clip-c"_s, u"V2"_s, 0, 0, 90);
    f.selection.select(u"clip-a"_s);
    f.selection.toggle(u"clip-b"_s);
    f.selection.toggle(u"clip-c"_s);

    f.timeline.removeSelected();
    CHECK(f.project.clips().isEmpty());
    CHECK(f.undo.undoText() == u"Remove 3 Clips"_s);

    REQUIRE(f.bus.execute(u"edit.undo"_s));
    CHECK(f.project.clips().size() == 3);
    CHECK_FALSE(f.undo.canUndo());

    REQUIRE(f.bus.execute(u"edit.redo"_s));
    CHECK(f.project.clips().isEmpty());
}

TEST_CASE("Every failed timeline action raises a toast, also the same error twice", "[timeline-controller]")
{
    TimelineFixture f;
    f.addClip(u"clip-a"_s, u"V1"_s, 0, 0, 90);
    f.addClip(u"clip-b"_s, u"V1"_s, 100, 0, 90);
    QSignalSpy toasts(&f.timeline, &TimelineController::toastRequested);

    CHECK_FALSE(f.timeline.moveClip(u"clip-b"_s, u"V1"_s, 50));
    CHECK_FALSE(f.timeline.moveClip(u"clip-b"_s, u"V1"_s, 50));
    REQUIRE(toasts.count() == 2);
    CHECK(toasts.at(0).at(0).toString() == toasts.at(1).at(0).toString());
    CHECK(toasts.at(1).at(0).toString().contains(u"overlap"_s));
    CHECK(toasts.at(1).at(1).toBool());

    CHECK_FALSE(f.timeline.addClip(u"media-video"_s, u"V1"_s, 10));
    CHECK(toasts.count() == 3);
}

TEST_CASE("S toggles snapping with a toast that says so", "[timeline-controller]")
{
    TimelineFixture f;
    REQUIRE(f.timeline.snapping());
    QSignalSpy toasts(&f.timeline, &TimelineController::toastRequested);
    QSignalSpy changed(&f.timeline, &TimelineController::snappingChanged);

    f.timeline.toggleSnapping();
    CHECK_FALSE(f.timeline.snapping());
    CHECK(changed.count() == 1);
    REQUIRE(toasts.count() == 1);
    CHECK(toasts.at(0).at(0).toString().startsWith(u"Snapping off"_s));
    CHECK_FALSE(toasts.at(0).at(1).toBool());

    f.timeline.toggleSnapping();
    CHECK(f.timeline.snapping());
    CHECK(toasts.at(1).at(0).toString() == u"Snapping on"_s);
}

TEST_CASE("The timeline scroll is clamped again when the project is reset or gets shorter", "[timeline-controller]")
{
    TimelineFixture f;
    f.timeline.setViewportWidth(400);
    f.timeline.zoomAt(TimelineController::kMaxPixelsPerFrame, 0);
    f.addClip(u"clip-a"_s, u"V1"_s, 0, 0, 90);
    f.addClip(u"clip-far"_s, u"V1"_s, 9000, 0, 90);
    f.timeline.setScrollX(1e9);
    const double farScroll = f.timeline.scrollX();
    REQUIRE(farScroll > 300000.0);

    // Removing the last clip shortens the content: the view moves back onto it.
    REQUIRE(f.project.removeClip(u"clip-far"_s));
    const double maxAfter = static_cast<double>(90 + 10 * 30) * f.timeline.pixelsPerFrame() - 400.0;
    CHECK(f.timeline.scrollX() <= maxAfter + 0.5);

    f.timeline.setScrollX(1e9);
    REQUIRE(f.timeline.scrollX() > 0.0);
    f.project.reset();
    CHECK(f.timeline.scrollX() == 0.0);
    CHECK(f.timeline.frameAtX(0) == 0);
}

TEST_CASE("Track rows come from the project and follow added tracks and their flags", "[timeline-controller]")
{
    TimelineFixture f;
    f.addClip(u"clip-a"_s, u"V1"_s, 0, 0, 90);
    CHECK(trackIds(f.timeline) == QStringList{u"V2"_s, u"V1"_s, u"A1"_s, u"A2"_s});
    CHECK(clipData(*f.timeline.model(), u"clip-a"_s, TimelineModel::TrackRowRole) == 1);
    QSignalSpy tracksChanged(&f.timeline, &TimelineController::tracksChanged);
    QSignalSpy rowsMoved(f.timeline.model(), &TimelineModel::dataChanged);

    REQUIRE(f.bus.execute(u"timeline.add_track"_s, QJsonObject{{u"kind"_s, u"video"_s}}));
    REQUIRE(f.bus.execute(u"timeline.add_track"_s, QJsonObject{{u"kind"_s, u"audio"_s}}));
    CHECK(trackIds(f.timeline) == QStringList{u"V3"_s, u"V2"_s, u"V1"_s, u"A1"_s, u"A2"_s, u"A3"_s});
    CHECK(tracksChanged.count() >= 2);
    CHECK(rowsMoved.count() >= 1);
    CHECK(clipData(*f.timeline.model(), u"clip-a"_s, TimelineModel::TrackRowRole) == 2);
    CHECK(f.timeline.trackKind(0) == u"video"_s);
    CHECK(f.timeline.trackKind(5) == u"audio"_s);

    // The header toggles run timeline.set_track_flags, one flag at a time.
    tracksChanged.clear();
    CHECK(f.timeline.setTrackMuted(u"A3"_s, true));
    CHECK(trackEntry(f.timeline, u"A3"_s).value(u"muted"_s) == true);
    CHECK(tracksChanged.count() >= 1);
    CHECK(clipData(*f.timeline.model(), u"clip-a"_s, TimelineModel::TrackOffRole) == false);
    CHECK(f.timeline.setTrackHidden(u"V1"_s, true));
    CHECK(trackEntry(f.timeline, u"V1"_s).value(u"hidden"_s) == true);
    CHECK(trackEntry(f.timeline, u"V1"_s).value(u"muted"_s) == false);
    CHECK(clipData(*f.timeline.model(), u"clip-a"_s, TimelineModel::TrackOffRole) == true);

    REQUIRE(f.bus.execute(u"edit.undo"_s));
    CHECK(trackEntry(f.timeline, u"V1"_s).value(u"hidden"_s) == false);
    CHECK(clipData(*f.timeline.model(), u"clip-a"_s, TimelineModel::TrackOffRole) == false);
}

TEST_CASE("Track rows scroll vertically when they do not fit", "[timeline-controller]")
{
    TimelineFixture f;
    const int rowHeight = f.timeline.trackHeight();
    f.timeline.setViewportHeight(rowHeight * 2.5);
    f.timeline.scrollTracksBy(1e6);
    const double maxScroll = 4 * rowHeight - rowHeight * 2.5;
    CHECK(f.timeline.scrollY() == maxScroll);
    CHECK(f.timeline.rowAtY(0) == 1);
    CHECK(f.timeline.rowAtY(rowHeight * 2.4) == 3);

    // A new track makes room to scroll further; a reset goes back to the top.
    REQUIRE(f.bus.execute(u"timeline.add_track"_s, QJsonObject{{u"kind"_s, u"audio"_s}}));
    f.timeline.scrollTracksBy(1e6);
    CHECK(f.timeline.scrollY() == maxScroll + rowHeight);
    f.project.reset();
    CHECK(f.timeline.scrollY() == 0.0);
    CHECK(f.timeline.rowAtY(0) == 0);
}
