# API tests: a real CommandBus with test commands behind an ApiServer on an ephemeral port.
find_package(Qt6 6.8 REQUIRED COMPONENTS Network WebSockets)

add_executable(mikserstudio-api-tests
    ApiTestSupport.cpp
    ApiTestSupport.h
    AuthTests.cpp
    DiscoveryFileTests.cpp
    EventsTests.cpp
    HttpRoutesTests.cpp
    ServerLifecycleTests.cpp
    main.cpp
)
target_link_libraries(mikserstudio-api-tests PRIVATE MikserStudio::api Qt6::Network Qt6::WebSockets Catch2::Catch2)
# The auth tests exercise private helpers of the api module.
target_include_directories(mikserstudio-api-tests PRIVATE "${PROJECT_SOURCE_DIR}/src/api/src")
if(WIN32)
    target_link_libraries(mikserstudio-api-tests PRIVATE advapi32)
endif()
mikserstudio_target_defaults(mikserstudio-api-tests)

catch_discover_tests(mikserstudio-api-tests DISCOVERY_MODE PRE_TEST)
