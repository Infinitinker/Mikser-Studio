# The plugins' specs (docs/plugins.md, "Tests"): one test that runs, in the Lua host, the spec/*.lua
# files of the built-in plugins and of the example, found when it runs, so a new spec needs no change
# here. The specs reach the services tests' fake AI tools through the HTTP job kinds.
find_package(Qt6 6.8 REQUIRED COMPONENTS HttpServer Network WebSockets)

add_executable(mikserstudio-plugin-specs
    PluginSpecTests.cpp
    main.cpp
    "${PROJECT_SOURCE_DIR}/tests/services/FakeAiTools.cpp"
    "${PROJECT_SOURCE_DIR}/tests/services/ServicesTestSupport.cpp"
)
target_link_libraries(mikserstudio-plugin-specs PRIVATE MikserStudio::lua MikserStudio::services MikserStudio::core
                                                        Qt6::HttpServer Qt6::Network Qt6::WebSockets Catch2::Catch2)
target_include_directories(mikserstudio-plugin-specs PRIVATE "${PROJECT_SOURCE_DIR}/tests/services")
target_compile_definitions(mikserstudio-plugin-specs PRIVATE
    MIKSERSTUDIO_PLUGINS_SOURCE="${PROJECT_SOURCE_DIR}/plugins")
mikserstudio_target_defaults(mikserstudio-plugin-specs)

catch_discover_tests(mikserstudio-plugin-specs DISCOVERY_MODE PRE_TEST PROPERTIES LABELS lua)
