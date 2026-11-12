// The properties panel: an In past the Out, or an Out before the In, slides the trim window in one undo step.
#include "PropertiesPanel.h"
#include "TimelineFixture.h"

#include <QSpinBox>

using namespace Qt::StringLiterals;
using mikserstudio::ui::PropertiesPanel;
using mikserstudio::ui::test::TimelineFixture;

namespace {

struct PanelFixture : TimelineFixture {
    PropertiesPanel panel{bus, project, selection};

    [[nodiscard]] std::pair<qint64, qint64> trim() const
    {
        const mikserstudio::core::Clip* clip = project.findClip(u"clip-a"_s);
        REQUIRE(clip);
        return {clip->in, clip->out};
    }
};

}  // namespace

TEST_CASE("Properties: an In past the Out slides the trim window in one undo step", "[properties]")
{
    PanelFixture f;
    f.addClip(u"clip-a"_s, u"V1"_s, 0, 10, 50);
    f.selection.select(u"clip-a"_s);
    REQUIRE(f.panel.shownClipId() == u"clip-a"_s);

    f.panel.inEditor()->setValue(60);
    f.panel.commitIn();
    CHECK(f.trim() == std::pair<qint64, qint64>{60, 100});
    CHECK(f.panel.errorText().isEmpty());
    CHECK(f.panel.inEditor()->value() == 60);
    CHECK(f.panel.outEditor()->value() == 100);

    REQUIRE(f.bus.execute(u"edit.undo"_s));
    CHECK(f.trim() == std::pair<qint64, qint64>{10, 50});
    CHECK_FALSE(f.undo.canUndo());

    // The Out stops at the end of the media.
    f.panel.inEditor()->setValue(280);
    f.panel.commitIn();
    CHECK(f.trim() == std::pair<qint64, qint64>{280, 300});
}

TEST_CASE("Properties: an Out before the In slides the trim window back", "[properties]")
{
    PanelFixture f;
    f.addClip(u"clip-a"_s, u"V1"_s, 0, 100, 140);
    f.selection.select(u"clip-a"_s);

    f.panel.outEditor()->setValue(90);
    f.panel.commitOut();
    CHECK(f.trim() == std::pair<qint64, qint64>{50, 90});

    // The In stops at the start of the media.
    f.panel.outEditor()->setValue(20);
    f.panel.commitOut();
    CHECK(f.trim() == std::pair<qint64, qint64>{0, 20});

    // An ordinary edit changes only the edited field.
    f.panel.outEditor()->setValue(70);
    f.panel.commitOut();
    CHECK(f.trim() == std::pair<qint64, qint64>{0, 70});
    f.panel.inEditor()->setValue(30);
    f.panel.commitIn();
    CHECK(f.trim() == std::pair<qint64, qint64>{30, 70});
}
