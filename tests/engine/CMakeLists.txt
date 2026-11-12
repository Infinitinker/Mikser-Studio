# Engine tests. They run on the offscreen Qt platform (QRhi Null backend) with the silent MLT
# output, so they need no GPU and no audio device. Tests using the sample video skip without it.
find_package(Qt6 REQUIRED COMPONENTS Test Widgets)
find_package(Mlt7 REQUIRED)

add_executable(mikserstudio-engine-tests
    ClipPlayerTests.cpp
    EngineTestSupport.cpp
    EngineTestSupport.h
    FrameMailboxTests.cpp
    MltMediaProbeTests.cpp
    MltResourceTests.cpp
    MltRuntimeTests.cpp
    MonitorPanelTests.cpp
    MonitorWindowTests.cpp
    OutputConsumerTests.cpp
    ShuttleTests.cpp
    SilentPacerTests.cpp
    TestMain.cpp
    TimecodeTests.cpp
    TimelinePlayerTests.cpp
)
target_link_libraries(mikserstudio-engine-tests PRIVATE MikserStudio::engine Mlt7::mlt++ Qt6::Test Qt6::Widgets Catch2::Catch2)
# Some tests exercise private helpers of the engine (which include MLT headers).
target_include_directories(mikserstudio-engine-tests PRIVATE "${PROJECT_SOURCE_DIR}/src/engine/src")
set(MIKSERSTUDIO_SAMPLE_VIDEO "D:/samples/sample-30s.mp4" CACHE FILEPATH "Sample video used by engine tests when present")
target_compile_definitions(mikserstudio-engine-tests PRIVATE MIKSERSTUDIO_SAMPLE_VIDEO="${MIKSERSTUDIO_SAMPLE_VIDEO}")
mikserstudio_target_defaults(mikserstudio-engine-tests)

catch_discover_tests(mikserstudio-engine-tests DISCOVERY_MODE PRE_TEST)

# The GPU test: the monitor on the machine's own platform and GPU, with the sample video. Labeled gpu,
# which the debug and ci test presets leave out; the gpu preset runs it (docs/development.md, "GPU test").
add_executable(mikserstudio-engine-gpu-tests
    EngineTestSupport.cpp
    EngineTestSupport.h
    gpu/MonitorGpuTests.cpp
    gpu/main.cpp
)
target_link_libraries(mikserstudio-engine-gpu-tests PRIVATE MikserStudio::engine Qt6::Widgets Catch2::Catch2)
target_include_directories(mikserstudio-engine-gpu-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(mikserstudio-engine-gpu-tests PRIVATE MIKSERSTUDIO_SAMPLE_VIDEO="${MIKSERSTUDIO_SAMPLE_VIDEO}")
mikserstudio_target_defaults(mikserstudio-engine-gpu-tests)

catch_discover_tests(mikserstudio-engine-gpu-tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS gpu TIMEOUT 120)
