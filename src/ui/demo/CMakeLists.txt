# mikser-studio-ui-demo: the shell with fake media, transport and AI (see main.cpp).
add_executable(mikser-studio-ui-demo WIN32
    DemoMonitorPanel.cpp
    DemoMonitorPanel.h
    DemoProject.cpp
    DemoProject.h
    FakeAiService.cpp
    FakeAiService.h
    FakeMediaProbe.cpp
    FakeMediaProbe.h
    FakeTransport.cpp
    FakeTransport.h
    Screenshot.cpp
    Screenshot.h
    main.cpp
)
target_link_libraries(mikser-studio-ui-demo PRIVATE MikserStudio::ui)
mikserstudio_target_defaults(mikser-studio-ui-demo)
