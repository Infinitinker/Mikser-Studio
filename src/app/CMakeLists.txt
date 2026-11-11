# The mikser-studio executable: wires core, engine, services, API and UI together.
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(mikser-studio WIN32
    AppCommands.cpp
    AppCommands.h
    AppId.h
    AppPaths.cpp
    AppPaths.h
    DesktopApp.cpp
    DesktopApp.h
    Logging.cpp
    Logging.h
    ShutdownSignals.cpp
    ShutdownSignals.h
    Studio.cpp
    Studio.h
    UnavailableProbe.h
    main.cpp
)
target_link_libraries(mikser-studio PRIVATE
    MikserStudio::core MikserStudio::engine MikserStudio::api MikserStudio::services MikserStudio::ui Qt6::Widgets)
target_compile_definitions(mikser-studio PRIVATE
    MIKSERSTUDIO_VERSION="${PROJECT_VERSION}"
    MIKSERSTUDIO_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
mikserstudio_target_defaults(mikser-studio)

# The headless mode logs to the console; keep a console build next to the GUI one on Windows.
if(WIN32)
    set_target_properties(mikser-studio PROPERTIES WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>)
endif()

install(TARGETS mikser-studio RUNTIME DESTINATION bin)
