// The timeline model: tracks run top to bottom, it follows clips added, changed and removed, marks selected
// clips, resets with the project and names its roles for QML.
#include "mikserstudio/core/Project.h"
#include "mikserstudio/ui/ClipSelection.h"
#include "mikserstudio/ui/Theme.h"

#include "TimelineModel.h"

#include <catch2/catch_test_macros.hpp>

#include <QColor>
#include <QSignalSpy>

using namespace Qt::StringLiterals;
using mikserstudio::core::Clip;
using mikserstudio::core::MediaItem;
using mikserstudio::core::MediaKind;
using mikserstudio::ui::TimelineModel;

namespace {

MediaItem media(const QString& id, const QString& name, MediaKind kind, QJsonObject provenance = {})
{
    MediaItem item;
    item.id = id;
    item.path = u"D:/media/"_s + name;
    item.name = name;
    item.info.kind = kind;
    item.info.durationFrames = 300;
    item.info.hasVideo = kind != MediaKind::Audio;
    item.info.hasAudio = kind != MediaKind::Image;
    item.provenance = std::move(provenance);
    return item;
}

Clip clip(const QString& id, const QString& mediaId, const QString& trackId, qint64 position, qint64 length)
{
    return Clip{.id = id, .mediaId = mediaId, .trackId = trackId, .position = position, .in = 0, .out = length};
}

struct TimelineFixture {
    mikserstudio::core::ProjectModel project;
    mikserstudio::ui::ClipSelection selection{project};
    TimelineModel model{project, selection};

    TimelineFixture()
    {
        REQUIRE(project.addMedia(media(u"media-video"_s, u"Interview.mp4"_s, MediaKind::Video)));
        REQUIRE(project.addMedia(media(u"media-audio"_s, u"Score.wav"_s, MediaKind::Audio)));
        REQUIRE(project.addMedia(
            media(u"media-ai"_s, u"Upscaled.mp4"_s, MediaKind::Video, QJsonObject{{u"jobId"_s, u"job-1"_s}})));
    }

    [[nodiscard]] QVariant data(const QString& clipId, int role) const
    {
        return model.data(model.index(model.rowOfClip(clipId)), role);
    }
};

}  // namespace

TEST_CASE("TimelineModel orders tracks top to bottom: video from the highest, then audio", "[timeline]")
{
    TimelineFixture f;
    CHECK(f.model.trackOrder() == QStringList{u"V2"_s, u"V1"_s, u"A1"_s, u"A2"_s});
    CHECK(f.model.rowOfTrack(u"V1"_s) == 1);
    CHECK(f.model.trackAtRow(2) == u"A1"_s);
    CHECK(f.model.rowOfTrack(u"X9"_s) == -1);
    CHECK(f.model.trackAtRow(9).isEmpty());
}

TEST_CASE("TimelineModel follows clips added, changed and removed", "[timeline]")
{
    TimelineFixture f;
    CHECK(f.model.rowCount() == 0);

    QSignalSpy inserted(&f.model, &TimelineModel::rowsInserted);
    QSignalSpy removed(&f.model, &TimelineModel::rowsRemoved);
    QSignalSpy changed(&f.model, &TimelineModel::dataChanged);

    REQUIRE(f.project.addClip(clip(u"clip-a"_s, u"media-video"_s, u"V1"_s, 0, 90)));
    REQUIRE(f.project.addClip(clip(u"clip-b"_s, u"media-audio"_s, u"A1"_s, 30, 120)));
    REQUIRE(f.project.addClip(clip(u"clip-c"_s, u"media-ai"_s, u"V2"_s, 60, 45)));
    CHECK(inserted.count() == 3);
    REQUIRE(f.model.rowCount() == 3);

    CHECK(f.data(u"clip-a"_s, TimelineModel::NameRole) == u"Interview.mp4"_s);
    CHECK(f.data(u"clip-a"_s, TimelineModel::TrackIdRole) == u"V1"_s);
    CHECK(f.data(u"clip-a"_s, TimelineModel::TrackRowRole) == 1);
    CHECK(f.data(u"clip-a"_s, TimelineModel::LengthRole).toLongLong() == 90);
    CHECK(f.data(u"clip-a"_s, TimelineModel::KindRole) == u"video"_s);
    CHECK(f.data(u"clip-a"_s, TimelineModel::GeneratedRole) == false);
    CHECK(f.data(u"clip-b"_s, TimelineModel::KindRole) == u"audio"_s);
    CHECK(f.data(u"clip-b"_s, TimelineModel::TrackRowRole) == 2);
    CHECK(f.data(u"clip-b"_s, TimelineModel::PositionRole).toLongLong() == 30);
    CHECK(f.data(u"clip-c"_s, TimelineModel::GeneratedRole) == true);

    const auto& theme = mikserstudio::ui::darkTheme();
    CHECK(f.data(u"clip-a"_s, TimelineModel::ColorRole).value<QColor>() == theme.clipVideo);
    CHECK(f.data(u"clip-b"_s, TimelineModel::ColorRole).value<QColor>() == theme.clipAudio);

    REQUIRE(f.project.moveClip(u"clip-a"_s, u"V2"_s, 200));
    CHECK(changed.count() >= 1);
    CHECK(f.data(u"clip-a"_s, TimelineModel::TrackIdRole) == u"V2"_s);
    CHECK(f.data(u"clip-a"_s, TimelineModel::TrackRowRole) == 0);
    CHECK(f.data(u"clip-a"_s, TimelineModel::PositionRole).toLongLong() == 200);

    REQUIRE(f.project.trimClip(u"clip-b"_s, 10, 50));
    CHECK(f.data(u"clip-b"_s, TimelineModel::InRole).toLongLong() == 10);
    CHECK(f.data(u"clip-b"_s, TimelineModel::OutRole).toLongLong() == 50);
    CHECK(f.data(u"clip-b"_s, TimelineModel::LengthRole).toLongLong() == 40);

    REQUIRE(f.project.removeClip(u"clip-b"_s));
    CHECK(removed.count() == 1);
    CHECK(f.model.rowCount() == 2);
    CHECK(f.model.rowOfClip(u"clip-b"_s) == -1);

    // Every row still points at a clip of the project.
    for (int row = 0; row < f.model.rowCount(); ++row)
        CHECK(f.project.findClip(f.model.clipIdAt(row)) != nullptr);
}

TEST_CASE("TimelineModel marks selected clips and resets with the project", "[timeline]")
{
    TimelineFixture f;
    REQUIRE(f.project.addClip(clip(u"clip-a"_s, u"media-video"_s, u"V1"_s, 0, 90)));
    REQUIRE(f.project.addClip(clip(u"clip-b"_s, u"media-video"_s, u"V1"_s, 90, 90)));

    QSignalSpy changed(&f.model, &TimelineModel::dataChanged);
    f.selection.select(u"clip-b"_s);
    CHECK(changed.count() >= 1);
    CHECK(f.data(u"clip-a"_s, TimelineModel::SelectedRole) == false);
    CHECK(f.data(u"clip-b"_s, TimelineModel::SelectedRole) == true);

    QSignalSpy reset(&f.model, &TimelineModel::modelReset);
    f.project.reset();
    CHECK(reset.count() == 1);
    CHECK(f.model.rowCount() == 0);
    CHECK(f.selection.isEmpty());
}

TEST_CASE("TimelineModel exposes its roles to QML by name", "[timeline]")
{
    TimelineFixture f;
    const QHash<int, QByteArray> roles = f.model.roleNames();
    for (const char* name : {"clipId", "mediaId", "name", "trackId", "trackRow", "position", "length", "inPoint",
                             "outPoint", "kind", "clipColor", "selected", "generated"}) {
        INFO(name);
        CHECK(roles.values().contains(QByteArray(name)));
    }
}
