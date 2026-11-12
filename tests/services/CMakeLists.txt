# Services tests: the AI client against an in-process HTTP server, the sidecar manager and the AI
# commands against a fake sidecar process, and (when ai/.venv is set up) the real Python sidecar.
find_package(Qt6 6.8 REQUIRED COMPONENTS HttpServer Network)

# Stands in for the Python sidecar: same command line, READY line and /v1 API, plus misbehaviors.
add_executable(mikserstudio-fake-sidecar fake/FakeSidecar.cpp)
target_link_libraries(mikserstudio-fake-sidecar PRIVATE Qt6::Core Qt6::HttpServer Qt6::Network)
set_target_properties(mikserstudio-fake-sidecar PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fake")
mikserstudio_target_defaults(mikserstudio-fake-sidecar)

add_executable(mikserstudio-services-tests
    AiClientTests.cpp
    AiCommandsTests.cpp
    AiJsonTests.cpp
    RealSidecarTests.cpp
    ServicesTestSupport.cpp
    ServicesTestSupport.h
    SidecarLaunchTests.cpp
    SidecarManagerTests.cpp
    main.cpp
)
target_link_libraries(mikserstudio-services-tests PRIVATE MikserStudio::services Qt6::HttpServer Qt6::Network Catch2::Catch2)
# The JSON and launch tests exercise private helpers of the services module.
target_include_directories(mikserstudio-services-tests PRIVATE "${PROJECT_SOURCE_DIR}/src/services/src")
target_compile_definitions(mikserstudio-services-tests PRIVATE
    MIKSERSTUDIO_FAKE_SIDECAR="$<TARGET_FILE:mikserstudio-fake-sidecar>"
    MIKSERSTUDIO_AI_PROJECT_DIR="${PROJECT_SOURCE_DIR}/ai")
add_dependencies(mikserstudio-services-tests mikserstudio-fake-sidecar)
mikserstudio_target_defaults(mikserstudio-services-tests)

catch_discover_tests(mikserstudio-services-tests DISCOVERY_MODE PRE_TEST)
