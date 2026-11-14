# Core tests.

# A native plugin used by the PluginManager tests. Its behavior is chosen by its "mode" setting.
add_library(mikserstudio-test-plugin-fixture MODULE fixture/FixturePlugin.cpp)
target_link_libraries(mikserstudio-test-plugin-fixture PRIVATE MikserStudio::sdk Qt6::Core)
set_target_properties(mikserstudio-test-plugin-fixture PROPERTIES
    OUTPUT_NAME fixture
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fixture"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fixture")
mikserstudio_target_defaults(mikserstudio-test-plugin-fixture)

# Unusable plugin libraries for the error paths: no entry symbol, and a future API version.
foreach(variant IN ITEMS noentry wrongapi)
    add_library(mikserstudio-test-plugin-${variant} MODULE fixture/BrokenPlugin.cpp)
    target_link_libraries(mikserstudio-test-plugin-${variant} PRIVATE MikserStudio::sdk)
    target_compile_definitions(mikserstudio-test-plugin-${variant} PRIVATE
        $<IF:$<STREQUAL:${variant},noentry>,MIKSERSTUDIO_FIXTURE_NO_ENTRY,MIKSERSTUDIO_FIXTURE_WRONG_API>)
    set_target_properties(mikserstudio-test-plugin-${variant} PROPERTIES
        OUTPUT_NAME ${variant}
        PREFIX ""
        CXX_VISIBILITY_PRESET hidden
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fixture"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fixture")
    mikserstudio_target_defaults(mikserstudio-test-plugin-${variant})
endforeach()

add_executable(mikserstudio-core-tests
    CommandBusTests.cpp
    EventBusTests.cpp
    InstanceTests.cpp
    JobManagerTests.cpp
    JsonSchemaTests.cpp
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
target_compile_definitions(mikserstudio-core-tests PRIVATE
    MIKSERSTUDIO_TEST_FIXTURE_PLUGIN="$<TARGET_FILE:mikserstudio-test-plugin-fixture>"
    MIKSERSTUDIO_TEST_NOENTRY_PLUGIN="$<TARGET_FILE:mikserstudio-test-plugin-noentry>"
    MIKSERSTUDIO_TEST_WRONGAPI_PLUGIN="$<TARGET_FILE:mikserstudio-test-plugin-wrongapi>")
add_dependencies(mikserstudio-core-tests
    mikserstudio-test-plugin-fixture mikserstudio-test-plugin-noentry mikserstudio-test-plugin-wrongapi)
mikserstudio_target_defaults(mikserstudio-core-tests)

catch_discover_tests(mikserstudio-core-tests DISCOVERY_MODE PRE_TEST)
