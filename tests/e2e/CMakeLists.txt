# End-to-end tests: the built mikser-studio, started as the E2E instance and driven from outside, as
# a user's tool drives it, also through the built MCP bridge; and the real Python AI service from ai/.venv,
# with ffmpeg, driven through the services module. Labeled e2e, which the debug and ci test presets leave
# out; the e2e preset runs them (docs/development.md, "End-to-end tests").
find_package(Qt6 6.8 REQUIRED COMPONENTS HttpServer Network)

add_executable(mikserstudio-e2e-tests
    ${PROJECT_SOURCE_DIR}/tests/services/ServicesTestSupport.cpp
    HeadlessTests.cpp
    RealSidecarTests.cpp
    main.cpp
)
target_link_libraries(mikserstudio-e2e-tests PRIVATE MikserStudio::services Qt6::HttpServer Qt6::Network
                                                      Catch2::Catch2)
# The real service's tests share the services tests' support (waiting, a media probe without the engine).
target_include_directories(mikserstudio-e2e-tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/services")
target_compile_definitions(mikserstudio-e2e-tests PRIVATE
    MIKSERSTUDIO_APP="$<TARGET_FILE:mikser-studio>"
    MIKSERSTUDIO_AI_PROJECT_DIR="${PROJECT_SOURCE_DIR}/ai")
add_dependencies(mikserstudio-e2e-tests mikser-studio)
if(MIKSERSTUDIO_WITH_MCP)
    target_sources(mikserstudio-e2e-tests PRIVATE McpBridgeTests.cpp)
    target_compile_definitions(mikserstudio-e2e-tests PRIVATE MIKSERSTUDIO_MCP="$<TARGET_FILE:mikser-studio-mcp>")
    add_dependencies(mikserstudio-e2e-tests mikser-studio-mcp)
endif()
mikserstudio_target_defaults(mikserstudio-e2e-tests)

# The app's tests take seconds. The real AI service's may take minutes: up to 130 s for a cold start,
# a 60 s encode and up to 300 s for the upscale job, so they get a timeout above those deadlines.
catch_discover_tests(mikserstudio-e2e-tests TEST_SPEC "~[real-sidecar]" DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS e2e TIMEOUT 120)
catch_discover_tests(mikserstudio-e2e-tests TEST_SPEC "[real-sidecar]" DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS e2e TIMEOUT 600)
