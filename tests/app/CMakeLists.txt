# App tests: the pieces of src/app that work without the whole studio, compiled in directly
# because src/app builds only the executable.
find_package(Qt6 6.8 REQUIRED COMPONENTS Gui)

add_executable(mikserstudio-app-tests
    LogFileTests.cpp
    ShutdownSignalsTests.cpp
    main.cpp
    "${PROJECT_SOURCE_DIR}/src/app/LogFile.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/Logging.cpp"
    "${PROJECT_SOURCE_DIR}/src/app/ShutdownSignals.cpp"
)
if(WIN32)
    target_sources(mikserstudio-app-tests PRIVATE "${PROJECT_SOURCE_DIR}/src/app/ProcessWindows.cpp")
endif()
target_include_directories(mikserstudio-app-tests PRIVATE "${PROJECT_SOURCE_DIR}/src/app")
target_link_libraries(mikserstudio-app-tests PRIVATE Qt6::Gui Catch2::Catch2)
mikserstudio_target_defaults(mikserstudio-app-tests)

# Without the fix the event loop dies and the test would wait forever.
catch_discover_tests(mikserstudio-app-tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 30)
