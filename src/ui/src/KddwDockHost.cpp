// The dock host on KDDockWidgets: panels as dock widgets, and workspaces saved and restored as layout
// files.
#include "mikserstudio/ui/KddwDockHost.h"

#include "DockViewFactory.h"
#include "Logging.h"
#include "PlaceholderPanel.h"
#include "WorkspaceFiles.h"
#include "WorkspacePresets.h"

#include <kddockwidgets/Config.h>
#include <kddockwidgets/KDDockWidgets.h>
#include <kddockwidgets/LayoutSaver.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/qtwidgets/views/DockWidget.h>
#include <kddockwidgets/qtwidgets/views/MainWindow.h>

#include <QCollator>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

KddwDockHost* KddwDockHost::s_instance = nullptr;

namespace {

constexpr int kSideWidth = 320;
constexpr int kBottomHeight = 300;
const auto kMainWindowName = u"mikserstudio.main"_s;

void configureKddw(KDDockWidgets::DockWidgetFactoryFunc factory)
{
    static bool configured = false;
    if (configured)
        return;
    configured = true;

    KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtWidgets);
    auto& config = KDDockWidgets::Config::self();
    using KDDockWidgets::Config;
    config.setFlags(Config::Flags(Config::Flag_Default) | Config::Flag_HideTitleBarWhenTabsVisible |
                    Config::Flag_AlwaysShowTabs | Config::Flag_AllowReorderTabs | Config::Flag_TabsHaveCloseButton |
                    Config::Flag_ShowButtonsOnTabBarIfTitleBarHidden | Config::Flag_AlwaysTitleBarWhenFloating |
                    Config::Flag_TitleBarHasMaximizeButton);
    config.setSeparatorThickness(4);
    config.setViewFactory(new DockViewFactory);  // Config takes ownership.
    config.setDockWidgetFactoryFunc(factory);
}

// "<id>" or "<id>#<n>" -> "<id>".
QString panelIdOf(const QString& dockName)
{
    const qsizetype hash = dockName.lastIndexOf(u'#');
    if (hash <= 0 || hash == dockName.size() - 1)
        return dockName;
    const QStringView suffix = QStringView(dockName).mid(hash + 1);
    const bool numeric = std::ranges::all_of(suffix, [](QChar c) { return c.isDigit(); });
    return numeric ? dockName.left(hash) : dockName;
}

QString instanceName(const QString& id, int instance)
{
    return instance <= 1 ? id : u"%1#%2"_s.arg(id).arg(instance);
}

KDDockWidgets::Location toLocation(Placement placement)
{
    switch (placement) {
    case Placement::Left:
        return KDDockWidgets::Location_OnLeft;
    case Placement::Right:
        return KDDockWidgets::Location_OnRight;
    case Placement::Bottom:
        return KDDockWidgets::Location_OnBottom;
    case Placement::Top:
    case Placement::Tab:
        break;
    }
    return KDDockWidgets::Location_OnTop;
}

KDDockWidgets::InitialOption sizeOption(Placement placement, int size)
{
    if (size <= 0)
        return {};
    const bool horizontal = placement == Placement::Left || placement == Placement::Right;
    return KDDockWidgets::InitialOption(horizontal ? QSize(size, 0) : QSize(0, size));
}

void collectPlacedDocks(const QJsonValue& value, QSet<QString>& names)
{
    if (value.isArray()) {
        for (const QJsonValue item : value.toArray())
            collectPlacedDocks(item, names);
        return;
    }
    const QJsonObject object = value.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (it.key() == u"dockWidgets"_s) {
            for (const QJsonValue name : it.value().toArray())
                names.insert(name.toString());
        } else {
            collectPlacedDocks(it.value(), names);
        }
    }
}

// The docks a saved layout puts in a tab group of a main or floating window, i.e. the open ones.
// Not LayoutSaver::openedDockWidgetsInLayout(): KDDockWidgets 2.5 decides "closed" by visibility,
// so it also lists every tab that was not the current one, and closing those would lose them.
QSet<QString> placedDocks(const QJsonObject& layout)
{
    QSet<QString> names;
    collectPlacedDocks(layout.value(u"mainWindows"_s), names);
    collectPlacedDocks(layout.value(u"floatingWindows"_s), names);
    names.remove(QString());
    return names;
}

}  // namespace

KddwDockHost::KddwDockHost(QString workspaceDirectory, QObject* parent)
    : DockHost(parent)
    , m_workspaceDirectory(std::move(workspaceDirectory))
{
    Q_ASSERT_X(!s_instance, "KddwDockHost", "only one dock host may exist at a time");
    configureKddw(&KddwDockHost::createDockForLayout);
    s_instance = this;
    m_mainWindow = std::make_unique<KDDockWidgets::QtWidgets::MainWindow>(kMainWindowName);
    m_mainWindow->setWindowTitle(u"Mikser Studio"_s);
}

KddwDockHost::~KddwDockHost()
{
    s_instance = nullptr;
    // Tearing down is not the user closing panels: observers hear nothing more from this host.
    blockSignals(true);
    // Close first so no dock is inside a layout or floating window while it is deleted, then
    // let KDDockWidgets finish the deferred deletions of the emptied groups and windows.
    closeAll();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const auto docks = m_docks.values();
    m_docks.clear();
    for (const QPointer<Dock>& dock : docks)
        delete dock.data();
    m_mainWindow.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void KddwDockHost::setCurrentWorkspace(const QString& name)
{
    if (name == m_currentWorkspace)
        return;
    m_currentWorkspace = name;
    Q_EMIT currentWorkspaceChanged(name);
}

QWidget* KddwDockHost::mainWindow() const
{
    return m_mainWindow.get();
}

void KddwDockHost::registerPanel(PanelDescriptor descriptor)
{
    const QString id = descriptor.id;
    m_panels.insert(id, std::move(descriptor));
    for (Dock* dock : docksOf(id)) {
        dock->setTitle(m_panels.value(id).title);
        if (qobject_cast<PlaceholderPanel*>(dock->widget()))
            setContent(dock, createContent(id, dock));
    }
    Q_EMIT panelsChanged();
}

void KddwDockHost::unregisterPanel(const QString& id)
{
    if (!m_panels.remove(id))
        return;
    for (Dock* dock : docksOf(id)) {
        if (dock->isOpen())
            dock->forceClose();
        setContent(dock, new PlaceholderPanel(id));
    }
    Q_EMIT panelsChanged();
}

QList<PanelDescriptor> KddwDockHost::panels() const
{
    QList<PanelDescriptor> list = m_panels.values();
    std::ranges::sort(list, {}, &PanelDescriptor::id);
    return list;
}

std::expected<void, QString> KddwDockHost::openPanel(const QString& id)
{
    const PanelDescriptor* panel = descriptor(id);
    Dock* dock = nullptr;
    if (!panel) {
        dock = firstDock(id);
        if (!dock)
            return std::unexpected(
                u"There is no panel \"%1\". It may come from a plugin that is not active."_s.arg(id));
    } else if (panel->unique) {
        dock = firstDock(id);
    } else {
        for (Dock* instance : docksOf(id)) {
            if (!instance->isOpen()) {
                dock = instance;
                break;
            }
        }
        if (!dock) {
            int instance = 1;
            while (m_docks.contains(instanceName(id, instance)))
                ++instance;
            dock = createDock(instanceName(id, instance));
        }
    }
    if (!dock)
        dock = createDock(id);

    if (!dock->isOpen()) {
        if (dock->dockWidget()->hasPreviousDockedLocation() || !panel)
            dock->open();
        else
            placeInDefaultArea(dock, panel->defaultArea);
    }
    dock->setAsCurrentTab();
    dock->raise();
    return {};
}

void KddwDockHost::closePanel(const QString& id)
{
    for (Dock* dock : docksOf(id)) {
        if (dock->isOpen())
            dock->forceClose();
    }
}

bool KddwDockHost::isPanelOpen(const QString& id) const
{
    const auto docks = docksOf(id);
    return std::ranges::any_of(docks, [](const Dock* dock) { return dock->isOpen(); });
}

QByteArray KddwDockHost::saveLayout() const
{
    KDDockWidgets::LayoutSaver saver;
    return saver.serializeLayout();
}

std::expected<void, QString> KddwDockHost::restoreLayout(const QByteArray& layout)
{
    return restore(layout, false);
}

QStringList KddwDockHost::workspaces() const
{
    QStringList names = builtInWorkspaces();
    QStringList saved;
    const QDir dir(m_workspaceDirectory);
    for (const QString& file : dir.entryList({u"*.json"_s}, QDir::Files)) {
        const QString name = workspaceNameFromFileName(file);
        if (name.isEmpty() || names.contains(name, Qt::CaseInsensitive) || saved.contains(name, Qt::CaseInsensitive))
            continue;
        saved.append(name);
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::ranges::sort(saved, collator);
    return names + saved;
}

std::expected<void, QString> KddwDockHost::saveWorkspace(const QString& name)
{
    if (const QString problem = checkWorkspaceName(name); !problem.isEmpty())
        return std::unexpected(problem);
    if (!QDir().mkpath(m_workspaceDirectory))
        return std::unexpected(u"Could not create the workspace folder %1. Check that it is writable."_s.arg(
            QDir::toNativeSeparators(m_workspaceDirectory)));
    const QString path = workspacePath(name);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(saveLayout()) < 0 || !file.commit())
        return std::unexpected(
            u"Could not save the workspace to %1: %2"_s.arg(QDir::toNativeSeparators(path), file.errorString()));
    const WorkspacePreset* preset = findWorkspacePreset(name);
    setCurrentWorkspace(preset ? preset->name : name);
    qCInfo(lcDocking) << "Saved workspace" << m_currentWorkspace;
    return {};
}

std::expected<void, QString> KddwDockHost::loadWorkspace(const QString& name)
{
    const WorkspacePreset* preset = findWorkspacePreset(name);
    const QString path = workspacePath(name);
    if (QFile::exists(path)) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return std::unexpected(
                u"Could not read the workspace file %1: %2"_s.arg(QDir::toNativeSeparators(path), file.errorString()));
        if (auto restored = restore(file.readAll(), true); !restored)
            return std::unexpected(u"The workspace \"%1\" could not be loaded. %2"_s.arg(name, restored.error()));
    } else if (preset) {
        applyPreset(*preset);
    } else {
        return std::unexpected(u"There is no workspace named \"%1\"."_s.arg(name));
    }
    setCurrentWorkspace(preset ? preset->name : name);
    return {};
}

QStringList KddwDockHost::builtInWorkspaces()
{
    QStringList names;
    for (const WorkspacePreset& preset : workspacePresets())
        names.append(preset.name);
    return names;
}

bool KddwDockHost::hasSavedWorkspace(const QString& name) const
{
    return checkWorkspaceName(name).isEmpty() && QFile::exists(workspacePath(name));
}

std::expected<void, QString> KddwDockHost::resetWorkspace(const QString& name)
{
    const WorkspacePreset* preset = findWorkspacePreset(name);
    if (!preset)
        return loadWorkspace(name);
    const QString path = workspacePath(name);
    if (QFile::exists(path) && !QFile::remove(path))
        return std::unexpected(u"Could not delete the saved workspace %1. Check that it is writable."_s.arg(
            QDir::toNativeSeparators(path)));
    applyPreset(*preset);
    setCurrentWorkspace(preset->name);
    return {};
}

std::expected<void, QString> KddwDockHost::setPanelFloating(const QString& id, bool floating)
{
    Dock* dock = nullptr;
    for (Dock* instance : docksOf(id)) {
        if (instance->isOpen()) {
            dock = instance;
            break;
        }
    }
    if (!dock)
        return std::unexpected(u"The panel \"%1\" is not open."_s.arg(id));
    if (dock->isFloating() != floating)
        dock->setFloating(floating);
    return {};
}

bool KddwDockHost::isPanelFloating(const QString& id) const
{
    const auto docks = docksOf(id);
    return std::ranges::any_of(docks, [](const Dock* dock) { return dock->isOpen() && dock->isFloating(); });
}

QWidget* KddwDockHost::panelWindow(const QString& id) const
{
    for (Dock* dock : docksOf(id)) {
        if (dock->isOpen())
            return dock->QWidget::window();
    }
    return nullptr;
}

QWidget* KddwDockHost::panelWidget(const QString& id) const
{
    Dock* dock = firstDock(id);
    return dock ? dock->widget() : nullptr;
}

bool KddwDockHost::isPlaceholder(const QString& id) const
{
    return qobject_cast<PlaceholderPanel*>(panelWidget(id)) != nullptr;
}

QStringList KddwDockHost::openPanels() const
{
    QStringList ids;
    for (auto it = m_docks.cbegin(); it != m_docks.cend(); ++it) {
        if (it.value() && it.value()->isOpen()) {
            const QString id = panelIdOf(it.key());
            if (!ids.contains(id))
                ids.append(id);
        }
    }
    ids.sort();
    return ids;
}

KDDockWidgets::Core::DockWidget* KddwDockHost::createDockForLayout(const QString& name)
{
    if (!s_instance)
        return nullptr;
    if (!s_instance->descriptor(panelIdOf(name)))
        qCInfo(lcDocking) << "The layout mentions the unknown panel" << name << "- showing a placeholder";
    return s_instance->createDock(name)->dockWidget();
}

const PanelDescriptor* KddwDockHost::descriptor(const QString& id) const
{
    const auto it = m_panels.constFind(id);
    return it == m_panels.cend() ? nullptr : &it.value();
}

QList<KddwDockHost::Dock*> KddwDockHost::docksOf(const QString& id) const
{
    QStringList names;
    for (auto it = m_docks.cbegin(); it != m_docks.cend(); ++it) {
        if (it.value() && panelIdOf(it.key()) == id)
            names.append(it.key());
    }
    // Instance order: "<id>", "<id>#2", "<id>#3", ...
    std::ranges::sort(
        names, [](const QString& a, const QString& b) { return a.size() != b.size() ? a.size() < b.size() : a < b; });
    QList<Dock*> docks;
    for (const QString& name : names)
        docks.append(m_docks.value(name).data());
    return docks;
}

KddwDockHost::Dock* KddwDockHost::firstDock(const QString& id) const
{
    const auto docks = docksOf(id);
    return docks.isEmpty() ? nullptr : docks.first();
}

KddwDockHost::Dock* KddwDockHost::createDock(const QString& name)
{
    const QString id = panelIdOf(name);
    auto* dock = new Dock(name);
    const PanelDescriptor* panel = descriptor(id);
    dock->setTitle(panel ? panel->title : PlaceholderPanel::title(id));
    setContent(dock, createContent(id, dock));
    connect(dock, &Dock::isOpenChanged, this, [this, id](bool open) {
        if (open)
            Q_EMIT panelOpened(id);
        else
            Q_EMIT panelClosed(id);
    });
    m_docks.insert(name, dock);
    return dock;
}

QWidget* KddwDockHost::createContent(const QString& id, Dock* dock)
{
    const PanelDescriptor* panel = descriptor(id);
    if (panel && panel->create) {
        if (QWidget* content = panel->create(dock))
            return content;
        qCWarning(lcDocking) << "The factory of panel" << id << "returned no widget";
    }
    return new PlaceholderPanel(id, dock);
}

void KddwDockHost::setContent(Dock* dock, QWidget* content)
{
    const QPointer<QWidget> previous = dock->widget();
    dock->setWidget(content);
    if (previous && previous != content)
        previous->deleteLater();
}

void KddwDockHost::placeInDefaultArea(Dock* dock, DockArea area)
{
    if (area == DockArea::Floating) {
        dock->open();  // A dock widget that is not in a layout opens as a floating window.
        return;
    }
    // Tab it with a docked panel that lives in the same area, like editors group monitors.
    QStringList names = m_docks.keys();
    names.sort();
    for (const QString& name : names) {
        Dock* other = m_docks.value(name);
        if (!other || other == dock || !other->isOpen() || other->isFloating())
            continue;
        const PanelDescriptor* otherPanel = descriptor(panelIdOf(name));
        if (otherPanel && otherPanel->defaultArea == area) {
            other->addDockWidgetAsTab(dock);
            return;
        }
    }
    switch (area) {
    case DockArea::Left:
        m_mainWindow->addDockWidget(dock, KDDockWidgets::Location_OnLeft, nullptr, QSize(kSideWidth, 0));
        break;
    case DockArea::Right:
        m_mainWindow->addDockWidget(dock, KDDockWidgets::Location_OnRight, nullptr, QSize(kSideWidth, 0));
        break;
    case DockArea::Bottom:
        m_mainWindow->addDockWidget(dock, KDDockWidgets::Location_OnBottom, nullptr, QSize(0, kBottomHeight));
        break;
    case DockArea::Center:
    case DockArea::Floating:
        m_mainWindow->addDockWidget(dock, KDDockWidgets::Location_OnTop);
        break;
    }
}

void KddwDockHost::applyPreset(const WorkspacePreset& preset)
{
    closeAll();
    QHash<QString, Dock*> placed;
    QList<Dock*> groupLeaders;
    for (const WorkspaceStep& step : preset.steps) {
        const PanelDescriptor* panel = descriptor(step.panelId);
        if (!panel)
            continue;
        Dock* dock = firstDock(step.panelId);
        if (!dock)
            dock = createDock(step.panelId);
        Dock* relative = placed.value(step.relativeTo, nullptr);
        if (step.placement == Placement::Tab) {
            if (relative)
                relative->addDockWidgetAsTab(dock);
            else
                placeInDefaultArea(dock, panel->defaultArea);
        } else {
            m_mainWindow->addDockWidget(dock, toLocation(step.placement), relative,
                                        sizeOption(step.placement, step.size));
            groupLeaders.append(dock);
        }
        placed.insert(step.panelId, dock);
    }
    for (Dock* leader : groupLeaders)
        leader->setAsCurrentTab();
    qCInfo(lcDocking) << "Applied the built-in workspace" << preset.name;
    Q_EMIT layoutChanged();
}

void KddwDockHost::closeAll()
{
    for (const QPointer<Dock>& dock : std::as_const(m_docks)) {
        if (dock && dock->isOpen())
            dock->forceClose();
    }
}

std::expected<void, QString> KddwDockHost::restore(const QByteArray& layout, bool keepMainWindowGeometry)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(layout, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::unexpected(u"The layout is not valid JSON (%1)."_s.arg(error.errorString()));
    KDDockWidgets::RestoreOptions options = KDDockWidgets::RestoreOption_CloseUnknownDockWidgets;
    if (keepMainWindowGeometry)
        options |= KDDockWidgets::RestoreOption_RelativeToMainWindow;
    KDDockWidgets::LayoutSaver saver(options);
    if (!saver.restoreLayout(layout))
        return std::unexpected(
            u"The layout could not be restored; it may come from an incompatible version of Mikser Studio."_s);
    // Panels the layout does not place would otherwise stay open (KDDockWidgets floats them).
    const QSet<QString> placed = placedDocks(document.object());
    for (auto it = m_docks.cbegin(); it != m_docks.cend(); ++it) {
        if (it.value() && it.value()->isOpen() && !placed.contains(it.key()))
            it.value()->forceClose();
    }
    Q_EMIT layoutChanged();
    return {};
}

QString KddwDockHost::workspacePath(const QString& name) const
{
    // Case-sensitive file systems would otherwise keep "editing.json" apart from "Editing.json".
    const WorkspacePreset* preset = findWorkspacePreset(name);
    return QDir(m_workspaceDirectory).filePath(workspaceFileName(preset ? preset->name : name));
}

}  // namespace mikserstudio::ui
