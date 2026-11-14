# The example plugin's tests: discovered from its manifest in plugins/examples/, started by the Lua
# host, greeting and following its settings.
add_executable(mikserstudio-plugin-hello-tests
    HelloPluginTests.cpp
    "${PROJECT_SOURCE_DIR}/tests/lua/LuaTestSupport.cpp"
)
target_link_libraries(mikserstudio-plugin-hello-tests PRIVATE MikserStudio::lua MikserStudio::core Catch2::Catch2WithMain)
# The Lua host tests' support (waitFor, Catch2 printers).
target_include_directories(mikserstudio-plugin-hello-tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/lua")
target_compile_definitions(mikserstudio-plugin-hello-tests PRIVATE
    MIKSERSTUDIO_EXAMPLE_PLUGINS="${PROJECT_SOURCE_DIR}/plugins/examples")
mikserstudio_target_defaults(mikserstudio-plugin-hello-tests)

catch_discover_tests(mikserstudio-plugin-hello-tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS lua)
