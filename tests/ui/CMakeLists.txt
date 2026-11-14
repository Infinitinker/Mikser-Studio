# UI tests. They run on the offscreen platform (see main.cpp).
find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(mikserstudio-ui-tests
    AiPanelTests.cpp
    CommandInvokerTests.cpp
    CommandMenuTests.cpp
    CommandPaletteTests.cpp
    KddwDockHostTests.cpp
    MediaBinPanelTests.cpp
    MessageBoxAnswer.h
    PluginPanelBridgeTests.cpp
    PluginsPanelTests.cpp
    PropertiesPanelTests.cpp
    ShellStateTests.cpp
    SchemaFormTests.cpp
    TestCommands.h
    TimelineControllerTests.cpp
    TimelineFixture.h
    TimelineModelTests.cpp
    TimelineViewTests.cpp
    main.cpp
)
target_link_libraries(mikserstudio-ui-tests PRIVATE MikserStudio::ui Catch2::Catch2 Qt6::Test)
# Several tests exercise private classes of the ui module (panels, models).
target_include_directories(mikserstudio-ui-tests PRIVATE "${PROJECT_SOURCE_DIR}/src/ui/src")
mikserstudio_target_defaults(mikserstudio-ui-tests)

catch_discover_tests(mikserstudio-ui-tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
