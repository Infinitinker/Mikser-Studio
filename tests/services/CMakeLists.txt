# Services tests: the AI engine, its ComfyUI backend, the AI commands, the HTTP job kinds and
# comfyui.run_prompt against a fake ComfyUI and an HTTP server in the test process; the fake Ollama is
# for the Ollama plugin's specs.
# The whole path through the app is tested in tests/e2e.
find_package(Qt6 6.8 REQUIRED COMPONENTS HttpServer Network WebSockets)

add_executable(mikserstudio-services-tests
    AiCommandsTests.cpp
    AiEngineTests.cpp
    ComfyUiJobsTests.cpp
    FakeAiTools.cpp
    FakeAiTools.h
    HttpJobsTests.cpp
    ServicesTestSupport.cpp
    ServicesTestSupport.h
    main.cpp
)
target_link_libraries(mikserstudio-services-tests PRIVATE MikserStudio::services Qt6::HttpServer Qt6::Network
                                                          Qt6::WebSockets Catch2::Catch2)
# The engine tests exercise private helpers of the services module.
target_include_directories(mikserstudio-services-tests PRIVATE "${PROJECT_SOURCE_DIR}/src/services/src")
mikserstudio_target_defaults(mikserstudio-services-tests)

catch_discover_tests(mikserstudio-services-tests DISCOVERY_MODE PRE_TEST)
