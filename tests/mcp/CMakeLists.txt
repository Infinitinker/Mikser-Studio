# MCP bridge tests: the bridge against a fake Mikser Studio API inside the test, on an ephemeral port, and
# the built mikser-studio-mcp on stdin and stdout.
find_package(Qt6 6.8 REQUIRED COMPONENTS HttpServer Network WebSockets)

add_executable(mikserstudio-mcp-tests
    BridgeTests.cpp
    DiscoveryTests.cpp
    FakeStudio.cpp
    FakeStudio.h
    McpTestSupport.cpp
    McpTestSupport.h
    ProgramTests.cpp
    main.cpp
)
target_link_libraries(mikserstudio-mcp-tests PRIVATE MikserStudio::mcp Qt6::HttpServer Qt6::Network Qt6::WebSockets
                                                      Catch2::Catch2)
target_compile_definitions(mikserstudio-mcp-tests PRIVATE MIKSERSTUDIO_MCP="$<TARGET_FILE:mikser-studio-mcp>")
add_dependencies(mikserstudio-mcp-tests mikser-studio-mcp)
mikserstudio_target_defaults(mikserstudio-mcp-tests)

catch_discover_tests(mikserstudio-mcp-tests DISCOVERY_MODE PRE_TEST)
