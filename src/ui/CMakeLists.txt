# MikserStudio::ui, see docs/architecture.md.
#
# The desktop shell: KDDockWidgets docking and workspaces, the theme, menus generated from command
# specs, the command palette, schema-generated forms and the built-in panels.
find_package(Qt6 REQUIRED COMPONENTS Widgets Quick QuickWidgets QuickControls2)
# The package exports its library as KDAB::kddockwidgets.
find_package(KDDockWidgets-qt6 REQUIRED)

add_library(mikserstudio-ui STATIC
    include/mikserstudio/ui/ClipSelection.h
    include/mikserstudio/ui/CommandPalette.h
    include/mikserstudio/ui/DockHost.h
    include/mikserstudio/ui/KddwDockHost.h
    include/mikserstudio/ui/PanelIds.h
    include/mikserstudio/ui/SchemaForm.h
    include/mikserstudio/ui/Shell.h
    include/mikserstudio/ui/Theme.h
    src/AiJobRow.cpp
    src/AiJobRow.h
    src/AiPanel.cpp
    src/AiPanel.h
    src/ClipSelection.cpp
    src/CommandInvoker.cpp
    src/CommandInvoker.h
    src/CommandLogModel.cpp
    src/CommandLogModel.h
    src/CommandMenus.cpp
    src/CommandMenus.h
    src/CommandPalette.cpp
    src/CommandPaletteModel.cpp
    src/CommandPaletteModel.h
    src/CommandStatus.cpp
    src/CommandStatus.h
    src/CommandText.cpp
    src/CommandText.h
    src/DockViewFactory.cpp
    src/DockViewFactory.h
    src/FuzzyMatch.cpp
    src/FuzzyMatch.h
    src/KddwDockHost.cpp
    src/LogPanel.cpp
    src/LogPanel.h
    src/Logging.cpp
    src/Logging.h
    src/MediaBinModel.cpp
    src/MediaBinModel.h
    src/MediaBinPanel.cpp
    src/MediaBinPanel.h
    src/PanelLayout.cpp
    src/PanelLayout.h
    src/ParamsDialog.cpp
    src/ParamsDialog.h
    src/PlaceholderPanel.cpp
    src/PlaceholderPanel.h
    src/PluginPanelBridge.cpp
    src/PluginPanelBridge.h
    src/PluginPanelHost.cpp
    src/PluginPanelHost.h
    src/PluginsPanel.cpp
    src/PluginsPanel.h
    src/PropertiesPanel.cpp
    src/PropertiesPanel.h
    src/SchemaEditors.cpp
    src/SchemaEditors.h
    src/SchemaForm.cpp
    src/Shell.cpp
    src/Theme.cpp
    src/ThemeStyleSheet.cpp
    src/Timecode.cpp
    src/Timecode.h
    src/TimelineController.cpp
    src/TimelineController.h
    src/TimelineModel.cpp
    src/TimelineModel.h
    src/TimelineRules.cpp
    src/TimelineRules.h
    src/TimelineView.cpp
    src/TimelineView.h
    src/WindowMenu.cpp
    src/WindowMenu.h
    src/WorkspaceFiles.cpp
    src/WorkspaceFiles.h
    src/WorkspaceMenu.cpp
    src/WorkspaceMenu.h
    src/WorkspacePresets.cpp
    src/WorkspacePresets.h
)
add_library(MikserStudio::ui ALIAS mikserstudio-ui)

# The timeline QML, served from qrc:/mikserstudio/ui/qml/.
qt_add_resources(mikserstudio-ui mikserstudio_ui_qml
    PREFIX /mikserstudio/ui
    FILES
        qml/Timeline.qml
        qml/TimelineClip.qml
        qml/TimelineRuler.qml
        qml/TrackToggle.qml)

target_include_directories(mikserstudio-ui
    PUBLIC include
    PRIVATE src)
target_link_libraries(mikserstudio-ui
    PUBLIC MikserStudio::core Qt6::Widgets Qt6::Quick Qt6::QuickWidgets Qt6::QuickControls2
    PRIVATE KDAB::kddockwidgets)
mikserstudio_target_defaults(mikserstudio-ui)

add_subdirectory(demo)
