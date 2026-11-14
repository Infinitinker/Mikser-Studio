# The plugins' specs (docs/plugins.md, "Tests"): one test that runs, in the Lua host, the spec/*.lua
# files of the built-in plugins and of the example, found when it runs, so a new spec needs no change
# here.
add_executable(mikserstudio-plugin-specs
    PluginSpecTests.cpp
    "${PROJECT_SOURCE_DIR}/tests/lua/LuaTestSupport.cpp"
)
target_link_libraries(mikserstudio-plugin-specs PRIVATE MikserStudio::lua MikserStudio::core Catch2::Catch2WithMain)
# The Lua host tests' support (waitFor, Catch2 printers).
target_include_directories(mikserstudio-plugin-specs PRIVATE "${PROJECT_SOURCE_DIR}/tests/lua")
target_compile_definitions(mikserstudio-plugin-specs PRIVATE
    MIKSERSTUDIO_PLUGINS_SOURCE="${PROJECT_SOURCE_DIR}/plugins")
mikserstudio_target_defaults(mikserstudio-plugin-specs)

catch_discover_tests(mikserstudio-plugin-specs DISCOVERY_MODE PRE_TEST PROPERTIES LABELS lua)
