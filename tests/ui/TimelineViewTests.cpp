#include "mikserstudio/ui/Theme.h"

#include "TimelineFixture.h"
#include "TimelineView.h"

#include <QQuickItem>
#include <QTest>

using namespace Qt::StringLiterals;
using mikserstudio::ui::TimelineView;
using mikserstudio::ui::test::TimelineFixture;

namespace {

struct ViewFixture : TimelineFixture {
    mikserstudio::ui::ThemeObject theme;
    TimelineView view{timeline, theme};

    ViewFixture()
    {
        addClip(u"clip-a"_s, u"V1"_s, 0, 0, 90);
        addClip(u"clip-b"_s, u"V1"_s, 120, 0, 90);
        view.resize(900, 300);
        view.show();
        REQUIRE(view.status() == QQuickWidget::Ready);
        QApplication::processEvents();
    }

    // The middle of a clip on V1 (row 1), in view coordinates.
    [[nodiscard]] QPoint clipCenter(qint64 frame) const
    {
        const double x = timeline.headerWidth() + timeline.xOfFrame(frame + 45);
        const double y = timeline.rulerHeight() + timeline.trackHeight() * 1.5 - timeline.scrollY();
        return QPoint(static_cast<int>(x), static_cast<int>(y));
    }
};

}  // namespace

TEST_CASE("A plain click on a clip of a multi-selection leaves only that clip selected", "[timeline-view]")
{
    ViewFixture f;
    f.selection.select(u"clip-b"_s);
    f.selection.toggle(u"clip-a"_s);
    REQUIRE(f.selection.clipIds() == QStringList{u"clip-b"_s, u"clip-a"_s});

    QTest::mouseClick(&f.view, Qt::LeftButton, Qt::NoModifier, f.clipCenter(0));
    CHECK(f.selection.clipIds() == QStringList{u"clip-a"_s});
    CHECK(f.selection.current() == u"clip-a"_s);

    // Ctrl+click still adds to the selection, and keeps it on release.
    QTest::mouseClick(&f.view, Qt::LeftButton, Qt::ControlModifier, f.clipCenter(120));
    CHECK(f.selection.clipIds() == QStringList{u"clip-a"_s, u"clip-b"_s});
}

TEST_CASE("The S key toggles snapping", "[timeline-view]")
{
    ViewFixture f;
    REQUIRE(f.timeline.snapping());
    QTest::keyClick(&f.view, Qt::Key_S);
    CHECK_FALSE(f.timeline.snapping());
    QTest::keyClick(&f.view, Qt::Key_S);
    CHECK(f.timeline.snapping());
}
