# The hello plugin's tests: discovered from its manifest in the build's plugin folder, activated, greeting
# and following its settings.
add_executable(mikserstudio-plugin-hello-tests HelloPluginTests.cpp)
target_link_libraries(mikserstudio-plugin-hello-tests PRIVATE MikserStudio::core Catch2::Catch2WithMain)
# The core tests' support (Catch2 printers, JSON written in a test).
target_include_directories(mikserstudio-plugin-hello-tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/core")
target_compile_definitions(mikserstudio-plugin-hello-tests PRIVATE
    MIKSERSTUDIO_HELLO_PLUGIN_ROOT="$<TARGET_FILE_DIR:mikserstudio-plugin-hello>/..")
add_dependencies(mikserstudio-plugin-hello-tests mikserstudio-plugin-hello)
mikserstudio_target_defaults(mikserstudio-plugin-hello-tests)

catch_discover_tests(mikserstudio-plugin-hello-tests DISCOVERY_MODE PRE_TEST)
