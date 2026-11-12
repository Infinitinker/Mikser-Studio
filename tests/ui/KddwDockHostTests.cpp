// The KDDockWidgets dock host: panels open, raise, float and close, layouts are saved and restored, and
// placeholders stand in for unknown panels until they appear.
#include "mikserstudio/ui/KddwDockHost.h"
#include "mikserstudio/ui/PanelIds.h"

#include "PlaceholderPanel.h"
#include "WorkspaceFiles.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;
using mikserstudio::ui::DockArea;
using mikserstudio::ui::KddwDockHost;
using mikserstudio::ui::PanelDescriptor;

namespace {

// Whether the panel is open in a window of its own, apart from the main window, as the user sees it.
bool isFloating(const KddwDockHost& host, const QString& id)
{
    const QWidget* window = host.panelWindow(id);
    return window != nullptr && window != host.mainWindow();
}

// Whether the panel shows the placeholder for a panel nobody registered.
bool isPlaceholder(const KddwDockHost& host, const QString& id)
{
    return qobject_cast<mikserstudio::ui::PlaceholderPanel*>(host.panelWidget(id)) != nullptr;
}

PanelDescriptor labelPanel(const QString& id, DockArea area = DockArea::Right, bool unique = true)
{
    return PanelDescriptor{
        .id = id,
        .title = u"Title of "_s + id,
        .defaultArea = area,
        .unique = unique,
        .create =
            [id](QWidget* parent) {
                auto* label = new QLabel(id, parent);
                label->setObjectName(id);
                return label;
            },
    };
}

struct HostFixture {
    QTemporaryDir dir;
    std::unique_ptr<KddwDockHost> host = std::make_unique<KddwDockHost>(dir.filePath(u"workspaces"_s));

    HostFixture()
    {
        host->mainWindow()->resize(1200, 800);
        host->mainWindow()->show();
    }

    void settle() const { QApplication::processEvents(); }
};

}  // namespace

TEST_CASE("KddwDockHost opens, raises and closes panels", "[docking]")
{
    HostFixture f;
    f.host->registerPanel(labelPanel(u"test/a"_s, DockArea::Left));
    f.host->registerPanel(labelPanel(u"test/b"_s, DockArea::Right));
    REQUIRE(f.host->panels().size() == 2);
    CHECK(f.host->panels().first().id == u"test/a"_s);

    QStringList opened;
    QStringList closed;
    QObject::connect(f.host.get(), &KddwDockHost::panelOpened, [&](const QString& id) { opened.append(id); });
    QObject::connect(f.host.get(), &KddwDockHost::panelClosed, [&](const QString& id) { closed.append(id); });

    REQUIRE(f.host->openPanel(u"test/a"_s));
    REQUIRE(f.host->openPanel(u"test/b"_s));
    f.settle();
    CHECK(f.host->isPanelOpen(u"test/a"_s));
    CHECK(f.host->isPanelOpen(u"test/b"_s));
    CHECK(opened.contains(u"test/a"_s));
    CHECK(f.host->panelWindow(u"test/a"_s) == f.host->mainWindow());
    REQUIRE(f.host->panelWidget(u"test/a"_s) != nullptr);
    CHECK(f.host->panelWidget(u"test/a"_s)->objectName() == u"test/a"_s);

    // Opening a unique panel again only raises it: the same widget stays.
    QWidget* first = f.host->panelWidget(u"test/a"_s);
    REQUIRE(f.host->openPanel(u"test/a"_s));
    CHECK(f.host->panelWidget(u"test/a"_s) == first);
    CHECK(f.host->isPanelOpen(u"test/b"_s));

    f.host->closePanel(u"test/b"_s);
    f.settle();
    CHECK_FALSE(f.host->isPanelOpen(u"test/b"_s));
    CHECK(closed.contains(u"test/b"_s));

    // Reopening brings back the same widget.
    QWidget* before = f.host->panelWidget(u"test/b"_s);
    REQUIRE(f.host->openPanel(u"test/b"_s));
    CHECK(f.host->panelWidget(u"test/b"_s) == before);

    const auto missing = f.host->openPanel(u"test/missing"_s);
    REQUIRE_FALSE(missing);
    CHECK(missing.error().contains(u"test/missing"_s));
}

TEST_CASE("KddwDockHost opens several instances of non-unique panels", "[docking]")
{
    HostFixture f;
    f.host->registerPanel(labelPanel(u"test/multi"_s, DockArea::Center, false));
    REQUIRE(f.host->openPanel(u"test/multi"_s));
    REQUIRE(f.host->openPanel(u"test/multi"_s));
    f.settle();
    const QByteArray layout = f.host->saveLayout();
    CHECK(layout.contains("test/multi#2"));
    f.host->closePanel(u"test/multi"_s);
    CHECK_FALSE(f.host->isPanelOpen(u"test/multi"_s));
}

TEST_CASE("KddwDockHost floats panels", "[docking]")
{
    HostFixture f;
    f.host->registerPanel(labelPanel(u"test/a"_s, DockArea::Left));
    f.host->registerPanel(labelPanel(u"test/b"_s, DockArea::Right));
    f.host->registerPanel(labelPanel(u"test/float"_s, DockArea::Floating));
    REQUIRE(f.host->openPanel(u"test/a"_s));
    REQUIRE(f.host->openPanel(u"test/b"_s));
    REQUIRE(f.host->openPanel(u"test/float"_s));
    f.settle();
    CHECK(isFloating(*f.host, u"test/float"_s));

    REQUIRE(f.host->setPanelFloating(u"test/b"_s, true));
    f.settle();
    CHECK(isFloating(*f.host, u"test/b"_s));
    QWidget* window = f.host->panelWindow(u"test/b"_s);
    REQUIRE(window != nullptr);
    CHECK(window != f.host->mainWindow());
    CHECK(window->isWindow());

    REQUIRE(f.host->setPanelFloating(u"test/b"_s, false));
    f.settle();
    CHECK_FALSE(isFloating(*f.host, u"test/b"_s));
    CHECK_FALSE(f.host->setPanelFloating(u"test/never-opened"_s, true));
}

TEST_CASE("KddwDockHost saves and restores layouts", "[docking]")
{
    HostFixture f;
    f.host->registerPanel(labelPanel(u"test/a"_s, DockArea::Left));
    f.host->registerPanel(labelPanel(u"test/b"_s, DockArea::Right));
    f.host->registerPanel(labelPanel(u"test/c"_s, DockArea::Bottom));
    REQUIRE(f.host->openPanel(u"test/a"_s));
    REQUIRE(f.host->openPanel(u"test/b"_s));
    REQUIRE(f.host->setPanelFloating(u"test/b"_s, true));
    f.settle();

    int layoutChanges = 0;
    QObject::connect(f.host.get(), &KddwDockHost::layoutChanged, [&] { ++layoutChanges; });

    const QByteArray layout = f.host->saveLayout();
    REQUIRE_FALSE(layout.isEmpty());

    f.host->closePanel(u"test/a"_s);
    f.host->closePanel(u"test/b"_s);
    REQUIRE(f.host->openPanel(u"test/c"_s));
    f.settle();

    REQUIRE(f.host->restoreLayout(layout));
    f.settle();
    CHECK(layoutChanges == 1);
    CHECK(f.host->isPanelOpen(u"test/a"_s));
    CHECK(f.host->isPanelOpen(u"test/b"_s));
    CHECK(isFloating(*f.host, u"test/b"_s));
    CHECK_FALSE(f.host->isPanelOpen(u"test/c"_s));

    const auto broken = f.host->restoreLayout("{not json");
    REQUIRE_FALSE(broken);
    CHECK(broken.error().contains(u"is damaged"_s));
    CHECK(f.host->isPanelOpen(u"test/a"_s));
}

TEST_CASE("KddwDockHost keeps background tabs open across a restart", "[docking]")
{
    const auto registerAll = [](KddwDockHost& host) {
        host.registerPanel(labelPanel(u"test/front"_s, DockArea::Right));
        host.registerPanel(labelPanel(u"test/back1"_s, DockArea::Right));
        host.registerPanel(labelPanel(u"test/back2"_s, DockArea::Right));
        host.registerPanel(labelPanel(u"test/closed"_s, DockArea::Left));
    };

    QByteArray layout;
    QTemporaryDir dir;
    {
        KddwDockHost host(dir.path());
        host.mainWindow()->resize(1200, 800);
        host.mainWindow()->show();
        registerAll(host);
        // Same default area, so they share one tab group; the last one opened is in front.
        REQUIRE(host.openPanel(u"test/back1"_s));
        REQUIRE(host.openPanel(u"test/back2"_s));
        REQUIRE(host.openPanel(u"test/front"_s));
        REQUIRE(host.openPanel(u"test/closed"_s));
        host.closePanel(u"test/closed"_s);
        QApplication::processEvents();
        REQUIRE(host.panelWidget(u"test/front"_s)->isVisible());
        REQUIRE_FALSE(host.panelWidget(u"test/back1"_s)->isVisible());
        layout = host.saveLayout();
    }
    QApplication::processEvents();

    KddwDockHost host(dir.path());
    host.mainWindow()->resize(1200, 800);
    host.mainWindow()->show();
    registerAll(host);
    REQUIRE(host.openPanel(u"test/closed"_s));  // Open before the restore; the layout says closed.
    REQUIRE(host.restoreLayout(layout));
    QApplication::processEvents();

    CHECK(host.isPanelOpen(u"test/front"_s));
    CHECK(host.isPanelOpen(u"test/back1"_s));
    CHECK(host.isPanelOpen(u"test/back2"_s));
    CHECK(host.panelWidget(u"test/front"_s)->isVisible());
    CHECK_FALSE(host.isPanelOpen(u"test/closed"_s));
}

TEST_CASE("KddwDockHost shows placeholders for unknown panels and replaces them later", "[docking]")
{
    QByteArray layout;
    QTemporaryDir dir;
    {
        KddwDockHost host(dir.path());
        host.mainWindow()->show();
        host.registerPanel(labelPanel(u"test/a"_s, DockArea::Left));
        host.registerPanel(labelPanel(u"plugin.gone/panel"_s, DockArea::Right));
        REQUIRE(host.openPanel(u"test/a"_s));
        REQUIRE(host.openPanel(u"plugin.gone/panel"_s));
        QApplication::processEvents();
        layout = host.saveLayout();
    }
    QApplication::processEvents();

    KddwDockHost host(dir.path());
    host.mainWindow()->show();
    host.registerPanel(labelPanel(u"test/a"_s, DockArea::Left));
    REQUIRE(host.restoreLayout(layout));
    QApplication::processEvents();

    CHECK(host.isPanelOpen(u"test/a"_s));
    CHECK_FALSE(isPlaceholder(host, u"test/a"_s));
    REQUIRE(host.isPanelOpen(u"plugin.gone/panel"_s));
    REQUIRE(isPlaceholder(host, u"plugin.gone/panel"_s));
    const auto* placeholder = qobject_cast<mikserstudio::ui::PlaceholderPanel*>(
        host.panelWidget(u"plugin.gone/panel"_s));
    REQUIRE(placeholder != nullptr);
    CHECK(mikserstudio::ui::PlaceholderPanel::message(placeholder->panelId()) ==
          u"This panel comes from plugin.gone, which is not installed."_s);
    CHECK(host.panels().size() == 1);

    // The plugin comes back: the placeholder becomes the real panel in place.
    host.registerPanel(labelPanel(u"plugin.gone/panel"_s, DockArea::Right));
    QApplication::processEvents();
    CHECK_FALSE(isPlaceholder(host, u"plugin.gone/panel"_s));
    CHECK(host.isPanelOpen(u"plugin.gone/panel"_s));
    CHECK(host.panelWidget(u"plugin.gone/panel"_s)->objectName() == u"plugin.gone/panel"_s);

    // Unregistering closes it and leaves a placeholder for the next time.
    host.unregisterPanel(u"plugin.gone/panel"_s);
    QApplication::processEvents();
    CHECK_FALSE(host.isPanelOpen(u"plugin.gone/panel"_s));
    CHECK(isPlaceholder(host, u"plugin.gone/panel"_s));
}

TEST_CASE("KddwDockHost saves, lists and loads named workspaces", "[docking]")
{
    HostFixture f;
    f.host->registerPanel(labelPanel(u"test/a"_s, DockArea::Left));
    f.host->registerPanel(labelPanel(u"test/b"_s, DockArea::Right));

    CHECK(f.host->workspaces() == QStringList{u"Editing"_s, u"Color"_s, u"AI"_s});

    REQUIRE(f.host->openPanel(u"test/a"_s));
    f.settle();
    REQUIRE(f.host->saveWorkspace(u"Only A / test"_s));
    CHECK(f.host->currentWorkspace() == u"Only A / test"_s);
    REQUIRE(f.host->openPanel(u"test/b"_s));
    f.settle();
    REQUIRE(f.host->saveWorkspace(u"Both"_s));

    CHECK(f.host->workspaces() == QStringList{u"Editing"_s, u"Color"_s, u"AI"_s, u"Both"_s, u"Only A / test"_s});
    CHECK(f.host->hasSavedWorkspace(u"Both"_s));

    REQUIRE(f.host->loadWorkspace(u"Only A / test"_s));
    f.settle();
    CHECK(f.host->isPanelOpen(u"test/a"_s));
    CHECK_FALSE(f.host->isPanelOpen(u"test/b"_s));

    REQUIRE(f.host->loadWorkspace(u"Both"_s));
    f.settle();
    CHECK(f.host->isPanelOpen(u"test/b"_s));
    CHECK(f.host->currentWorkspace() == u"Both"_s);

    CHECK_FALSE(f.host->loadWorkspace(u"Nope"_s));
    CHECK_FALSE(f.host->saveWorkspace(u"  "_s));

    // A corrupt file is reported, not crashed on.
    QFile corrupt(QDir(f.host->workspaceDirectory()).filePath(u"Broken.json"_s));
    REQUIRE(corrupt.open(QIODevice::WriteOnly));
    corrupt.write("[1, 2");
    corrupt.close();
    const auto broken = f.host->loadWorkspace(u"Broken"_s);
    REQUIRE_FALSE(broken);
    CHECK(broken.error().contains(u"Broken"_s));
}

TEST_CASE("KddwDockHost applies and resets the built-in workspaces", "[docking]")
{
    using namespace mikserstudio::ui::panel_ids;
    HostFixture f;
    for (QStringView id : {kMediaBin, kTimeline, kProperties, kAi, kLog, kPlugins, kProgramMonitor})
        f.host->registerPanel(labelPanel(id.toString()));

    REQUIRE(f.host->loadWorkspace(u"Editing"_s));
    f.settle();
    CHECK(f.host->isPanelOpen(kTimeline.toString()));
    CHECK(f.host->isPanelOpen(kMediaBin.toString()));
    CHECK(f.host->isPanelOpen(kProgramMonitor.toString()));
    CHECK(f.host->isPanelOpen(kLog.toString()));
    // The source monitor is not registered here; the workspace skips it.
    CHECK_FALSE(f.host->isPanelOpen(kSourceMonitor.toString()));
    CHECK(f.host->currentWorkspace() == u"Editing"_s);

    REQUIRE(f.host->loadWorkspace(u"color"_s));
    f.settle();
    CHECK(f.host->currentWorkspace() == u"Color"_s);
    CHECK(f.host->isPanelOpen(kProgramMonitor.toString()));
    CHECK_FALSE(f.host->isPanelOpen(kLog.toString()));
    CHECK_FALSE(f.host->isPanelOpen(kAi.toString()));

    // A saved file overrides the built-in definition until the workspace is reset.
    REQUIRE(f.host->openPanel(kLog.toString()));
    REQUIRE(f.host->saveWorkspace(u"Color"_s));
    REQUIRE(f.host->loadWorkspace(u"AI"_s));
    REQUIRE(f.host->loadWorkspace(u"Color"_s));
    f.settle();
    CHECK(f.host->isPanelOpen(kLog.toString()));
    CHECK(f.host->workspaces().count(u"Color"_s) == 1);

    REQUIRE(f.host->resetWorkspace(u"Color"_s));
    f.settle();
    CHECK_FALSE(f.host->hasSavedWorkspace(u"Color"_s));
    CHECK_FALSE(f.host->isPanelOpen(kLog.toString()));

    REQUIRE(f.host->loadWorkspace(u"AI"_s));
    f.settle();
    CHECK(f.host->isPanelOpen(kAi.toString()));
    CHECK(f.host->isPanelOpen(kLog.toString()));
}

TEST_CASE("KddwDockHost saves a built-in workspace typed in another case under the built-in name", "[docking]")
{
    using namespace mikserstudio::ui::panel_ids;
    HostFixture f;
    for (QStringView id : {kMediaBin, kTimeline, kLog})
        f.host->registerPanel(labelPanel(id.toString()));
    const QDir workspaces(f.dir.filePath(u"workspaces"_s));

    REQUIRE(f.host->saveWorkspace(u"editing"_s));
    CHECK(f.host->currentWorkspace() == u"Editing"_s);
    // The file name matters on case-sensitive file systems: it must be the one "Editing" loads.
    CHECK(workspaces.entryList({u"*.json"_s}, QDir::Files) ==
          QStringList{mikserstudio::ui::workspaceFileName(u"Editing"_s)});
    CHECK(f.host->hasSavedWorkspace(u"Editing"_s));
    CHECK(f.host->hasSavedWorkspace(u"EDITING"_s));
    CHECK(f.host->workspaces().count(u"Editing"_s) == 1);

    REQUIRE(f.host->resetWorkspace(u"Editing"_s));
    CHECK(workspaces.entryList({u"*.json"_s}, QDir::Files).isEmpty());
}
