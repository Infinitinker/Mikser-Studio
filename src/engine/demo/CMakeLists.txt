# mikser-studio-monitor-demo <file> [--self-test <seconds> <out.png>]: a docked source monitor playing one
# file. The self-test plays, floats and re-docks the monitor, then reads the rendered frame back.
add_executable(mikser-studio-monitor-demo
    DemoWindow.cpp
    DemoWindow.h
    SelfTest.cpp
    SelfTest.h
    main.cpp
)
target_link_libraries(mikser-studio-monitor-demo PRIVATE MikserStudio::engine Qt6::Widgets)
mikserstudio_target_defaults(mikser-studio-monitor-demo)
