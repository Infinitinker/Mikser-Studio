# Core tests.

add_executable(mikserstudio-core-tests
    AppInfoTests.cpp
    CommandBusTests.cpp
    EventBusTests.cpp
    FakePluginRuntime.h
    InstanceTests.cpp
    JobManagerTests.cpp
    JsonSchemaTests.cpp
    PluginCommandsTests.cpp
    PluginManagerTests.cpp
    PluginManifestTests.cpp
    ProjectCommandsTests.cpp
    ProjectMigrationsTests.cpp
    ProjectModelTests.cpp
    TestSupport.h
    TransportCommandsTests.cpp
    UndoStackTests.cpp
)
target_link_libraries(mikserstudio-core-tests PRIVATE MikserStudio::core Catch2::Catch2WithMain)
# The migration tests exercise a private helper of the core.
target_include_directories(mikserstudio-core-tests PRIVATE "${PROJECT_SOURCE_DIR}/src/core/src")
mikserstudio_target_defaults(mikserstudio-core-tests)

catch_discover_tests(mikserstudio-core-tests DISCOVERY_MODE PRE_TEST)
