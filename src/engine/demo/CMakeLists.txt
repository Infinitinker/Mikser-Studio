# mikser-studio-monitor-demo <file>: a docked source monitor playing one file. Its automated counterpart
# is the gpu-labeled engine test (tests/engine/gpu).
add_executable(mikser-studio-monitor-demo
    DemoWindow.cpp
    DemoWindow.h
    main.cpp
)
target_link_libraries(mikser-studio-monitor-demo PRIVATE MikserStudio::engine Qt6::Widgets)
mikserstudio_target_defaults(mikser-studio-monitor-demo)
