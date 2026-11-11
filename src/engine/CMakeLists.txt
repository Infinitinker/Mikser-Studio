# MikserStudio::engine, see docs/architecture.md.
#
# MLT runtime and media probe, playback engines (source clip, program timeline) and the QRhi
# monitor. QRhi is only reachable through Qt::GuiPrivate (<rhi/qrhi.h> is a semi-public API).
# We use QRhi knowingly; it is tied to the Qt minor version (see docs/stack-recommendation.md).
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 REQUIRED COMPONENTS Gui GuiPrivate Widgets ShaderTools)
find_package(Mlt7 REQUIRED)

add_library(mikserstudio-engine STATIC
    include/mikserstudio/engine/ClipPlayer.h
    include/mikserstudio/engine/FrameSink.h
    include/mikserstudio/engine/MltMediaProbe.h
    include/mikserstudio/engine/MltPlayer.h
    include/mikserstudio/engine/MltRuntime.h
    include/mikserstudio/engine/MonitorPanel.h
    include/mikserstudio/engine/MonitorWindow.h
    include/mikserstudio/engine/Timecode.h
    include/mikserstudio/engine/TimelinePlayer.h
    src/ClipPlayer.cpp
    src/FrameConversion.cpp
    src/FrameConversion.h
    src/FrameMailbox.cpp
    src/FrameMailbox.h
    src/Logging.cpp
    src/Logging.h
    src/MltLog.cpp
    src/MltLog.h
    src/MltMediaProbe.cpp
    src/MltPaths.cpp
    src/MltPaths.h
    src/MltPlayer.cpp
    src/MltProfile.cpp
    src/MltProfile.h
    src/MltResource.cpp
    src/MltResource.h
    src/MltRuntime.cpp
    src/MonitorPanel.cpp
    src/MonitorRenderer.cpp
    src/MonitorRenderer.h
    src/MonitorWindow.cpp
    src/OutputConsumer.cpp
    src/OutputConsumer.h
    src/RhiBackend.cpp
    src/RhiBackend.h
    src/Shuttle.cpp
    src/Shuttle.h
    src/SilentPacer.cpp
    src/SilentPacer.h
    src/Timecode.cpp
    src/TimelineBuilder.cpp
    src/TimelineBuilder.h
    src/TimelinePlayer.cpp
    src/TransportBar.cpp
    src/TransportBar.h
)
add_library(MikserStudio::engine ALIAS mikserstudio-engine)

target_include_directories(mikserstudio-engine
    PUBLIC include
    PRIVATE src)
target_link_libraries(mikserstudio-engine
    PUBLIC MikserStudio::core Qt6::Gui Qt6::Widgets
    PRIVATE Qt6::GuiPrivate Mlt7::mlt++)
mikserstudio_target_defaults(mikserstudio-engine)

qt_add_shaders(mikserstudio-engine "mikserstudio-engine-shaders"
    PREFIX "/mikserstudio/engine"
    BASE shaders
    FILES
        shaders/monitor.vert
        shaders/monitor.frag)

# Where MLT was installed when Mikser Studio was built: the last resort for development builds, whose
# executables do not sit next to lib/mlt and share/mlt (see MltPaths.cpp for the lookup order).
cmake_path(GET Mlt7_DIR PARENT_PATH _mikserstudio_mlt_cmake_dir)
cmake_path(GET _mikserstudio_mlt_cmake_dir PARENT_PATH _mikserstudio_mlt_lib_dir)
cmake_path(GET _mikserstudio_mlt_lib_dir PARENT_PATH _mikserstudio_mlt_prefix)
set(MIKSERSTUDIO_MLT_BUILD_REPOSITORY "")
foreach(candidate IN ITEMS "${_mikserstudio_mlt_lib_dir}/mlt-7" "${_mikserstudio_mlt_lib_dir}/mlt")
    if(NOT MIKSERSTUDIO_MLT_BUILD_REPOSITORY AND IS_DIRECTORY "${candidate}")
        set(MIKSERSTUDIO_MLT_BUILD_REPOSITORY "${candidate}")
    endif()
endforeach()
set(MIKSERSTUDIO_MLT_BUILD_DATA "")
foreach(candidate IN ITEMS "${_mikserstudio_mlt_prefix}/share/mlt-7" "${_mikserstudio_mlt_prefix}/share/mlt")
    if(NOT MIKSERSTUDIO_MLT_BUILD_DATA AND IS_DIRECTORY "${candidate}")
        set(MIKSERSTUDIO_MLT_BUILD_DATA "${candidate}")
    endif()
endforeach()
target_compile_definitions(mikserstudio-engine PRIVATE
    MIKSERSTUDIO_MLT_BUILD_REPOSITORY="${MIKSERSTUDIO_MLT_BUILD_REPOSITORY}"
    MIKSERSTUDIO_MLT_BUILD_DATA="${MIKSERSTUDIO_MLT_BUILD_DATA}")

add_subdirectory(demo)
