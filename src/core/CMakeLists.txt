# MikserStudio::core, see docs/architecture.md.
add_library(mikserstudio-core STATIC
    include/mikserstudio/core/Command.h
    include/mikserstudio/core/CommandBus.h
    include/mikserstudio/core/EventBus.h
    include/mikserstudio/core/Instance.h
    include/mikserstudio/core/JobManager.h
    include/mikserstudio/core/JsonSchema.h
    include/mikserstudio/core/NetworkEntry.h
    include/mikserstudio/core/PluginCommands.h
    include/mikserstudio/core/PluginManager.h
    include/mikserstudio/core/PluginRuntime.h
    include/mikserstudio/core/Project.h
    include/mikserstudio/core/ProjectCommands.h
    include/mikserstudio/core/ProjectSnapshot.h
    include/mikserstudio/core/Transport.h
    include/mikserstudio/core/UndoStack.h
    include/mikserstudio/core/UserFolders.h
    src/CommandBus.cpp
    src/CommandSpecCheck.cpp
    src/CommandSpecCheck.h
    src/EventBus.cpp
    src/Instance.cpp
    src/JobManager.cpp
    src/JsonSchema.cpp
    src/Logging.cpp
    src/Logging.h
    src/MediaCommands.cpp
    src/NetworkEntry.cpp
    src/PluginCommands.cpp
    src/PluginManager.cpp
    src/PluginManifest.cpp
    src/PluginSettingsStore.cpp
    src/PluginSettingsStore.h
    src/ProjectCommands.cpp
    src/ProjectCommandsPrivate.h
    src/ProjectEvents.cpp
    src/ProjectJson.cpp
    src/ProjectJson.h
    src/ProjectMigrations.cpp
    src/ProjectMigrations.h
    src/ProjectModel.cpp
    src/ProjectSnapshot.cpp
    src/TimelineCommands.cpp
    src/TransportCommands.cpp
    src/UndoStack.cpp
    src/UserFolders.cpp
)
add_library(MikserStudio::core ALIAS mikserstudio-core)

target_include_directories(mikserstudio-core
    PUBLIC include
    PRIVATE src)
target_link_libraries(mikserstudio-core PUBLIC Qt6::Core)
# The public headers use std::expected.
target_compile_features(mikserstudio-core PUBLIC cxx_std_23)
mikserstudio_target_defaults(mikserstudio-core)
# The release build runs as the installed app when MIKSERSTUDIO_INSTANCE is unset; every other build as
# Mikser Studio Dev (Instance.h).
if(MIKSERSTUDIO_INSTALLED_APP)
    target_compile_definitions(mikserstudio-core PRIVATE MIKSERSTUDIO_INSTALLED_APP)
endif()
