// The media bin panel: the clip names get the room first, in the built-in workspaces and when the panel is narrow.
#include "mikserstudio/ui/KddwDockHost.h"
#include "mikserstudio/ui/PanelIds.h"

#include "MediaBinModel.h"
#include "MediaBinPanel.h"
#include "TimelineFixture.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QApplication>
#include <QFontMetrics>
#include <QLabel>
#include <QTemporaryDir>
#include <QTreeView>

using namespace Qt::StringLiterals;
using mikserstudio::core::MediaKind;
using mikserstudio::ui::DockArea;
using mikserstudio::ui::KddwDockHost;
using mikserstudio::ui::MediaBinModel;
using mikserstudio::ui::MediaBinPanel;
using mikserstudio::ui::test::TimelineFixture;

namespace {

const auto kTypicalName = u"Interview_CamA_take03.mp4"_s;

// A project with media items named as people name them.
struct MediaFixture : TimelineFixture {
    MediaFixture()
    {
        add(kTypicalName, MediaKind::Video, 54'000);
        add(u"Voiceover final mix.wav"_s, MediaKind::Audio, 2'400);
        add(u"Title card.png"_s, MediaKind::Image, 125);
    }

    void add(const QString& name, MediaKind kind, qint64 frames)
    {
        mikserstudio::core::MediaItem media;
        media.id = u"media-"_s + name;
        media.path = QDir::rootPath() + u"media/"_s + name;
        media.name = name;
        media.info.kind = kind;
        media.info.durationFrames = frames;
        REQUIRE(project.addMedia(media));
    }
};

// The width the name column needs to show the typical name whole: its text, the kind swatch and
// the theme's item padding.
int typicalNameWidth(const QTreeView& view)
{
    return view.fontMetrics().horizontalAdvance(kTypicalName) + view.iconSize().width() + 24;
}

}  // namespace

TEST_CASE("Media bin: the built-in workspaces leave room for a typical clip name", "[media-bin][docking]")
{
    using namespace mikserstudio::ui::panel_ids;
    const QString workspace = GENERATE(u"Editing"_s, u"AI"_s);

    MediaFixture f;
    QTemporaryDir dir;
    KddwDockHost host(dir.filePath(u"workspaces"_s));
    host.mainWindow()->resize(1600, 960);
    host.mainWindow()->show();
    host.registerPanel({kMediaBin.toString(), u"Media bin"_s, DockArea::Left, true,
                        [&f](QWidget* parent) { return new MediaBinPanel(f.bus, f.project, parent); }});
    for (const QStringView id : {kSourceMonitor, kProgramMonitor, kTimeline, kProperties, kAi, kLog, kPlugins}) {
        host.registerPanel({id.toString(), id.toString(), DockArea::Right, true,
                            [id](QWidget* parent) { return new QLabel(id.toString(), parent); }});
    }
    REQUIRE(host.loadWorkspace(workspace));
    QApplication::processEvents();

    const auto* panel = qobject_cast<MediaBinPanel*>(host.panelWidget(kMediaBin.toString()));
    REQUIRE(panel);
    const QTreeView* view = panel->view();
    INFO(workspace.toStdString() << ": the media bin is " << panel->width() << " wide, its name column "
                                 << view->columnWidth(MediaBinModel::NameColumn));
    CHECK(view->columnWidth(MediaBinModel::NameColumn) >= typicalNameWidth(*view));
}

TEST_CASE("Media bin: the kind column makes way for the names when the panel is narrow", "[media-bin]")
{
    MediaFixture f;
    MediaBinPanel panel(f.bus, f.project);
    panel.resize(300, 400);
    panel.show();
    QApplication::processEvents();
    const QTreeView* view = panel.view();
    CHECK(view->isColumnHidden(MediaBinModel::KindColumn));
    CHECK(view->columnWidth(MediaBinModel::NameColumn) >= typicalNameWidth(*view));

    panel.resize(480, 400);
    QApplication::processEvents();
    CHECK_FALSE(view->isColumnHidden(MediaBinModel::KindColumn));
    CHECK(view->columnWidth(MediaBinModel::NameColumn) >= typicalNameWidth(*view));
}
