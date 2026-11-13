# End-to-end tests: the built mikser-studio, started as the E2E instance and driven from outside, as a user's
# tool drives it, with a fake ComfyUI in the test process for ai.run. Labeled e2e, which the debug and ci
# test presets leave out; the e2e preset runs them (docs/development.md, "End-to-end tests").
find_package(Qt6 6.8 REQUIRED COMPONENTS HttpServer Network WebSockets)

add_executable(mikserstudio-e2e-tests
    ${PROJECT_SOURCE_DIR}/tests/services/FakeAiTools.cpp
    ${PROJECT_SOURCE_DIR}/tests/services/ServicesTestSupport.cpp
    AiRunTests.cpp
    HeadlessTests.cpp
    main.cpp
)
target_link_libraries(mikserstudio-e2e-tests PRIVATE MikserStudio::core Qt6::HttpServer Qt6::Network Qt6::WebSockets
                                                      Catch2::Catch2)
# The fake AI tools and the waiting helpers come from the services tests.
target_include_directories(mikserstudio-e2e-tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/services")
target_compile_definitions(mikserstudio-e2e-tests PRIVATE MIKSERSTUDIO_APP="$<TARGET_FILE:mikser-studio>")
add_dependencies(mikserstudio-e2e-tests mikser-studio)
mikserstudio_target_defaults(mikserstudio-e2e-tests)

catch_discover_tests(mikserstudio-e2e-tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS e2e TIMEOUT 180)
