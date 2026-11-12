# End-to-end tests: the built mikser-studio, started as the E2E instance and driven from outside, as
# a user's tool drives it. Labeled e2e, which the debug and ci test presets leave out; the e2e preset
# runs them (docs/development.md, "End-to-end tests").
find_package(Qt6 6.8 REQUIRED COMPONENTS Network)

add_executable(mikserstudio-e2e-tests
    HeadlessTests.cpp
    main.cpp
)
target_link_libraries(mikserstudio-e2e-tests PRIVATE Qt6::Network Catch2::Catch2)
target_compile_definitions(mikserstudio-e2e-tests PRIVATE MIKSERSTUDIO_APP="$<TARGET_FILE:mikser-studio>")
add_dependencies(mikserstudio-e2e-tests mikser-studio)
mikserstudio_target_defaults(mikserstudio-e2e-tests)

catch_discover_tests(mikserstudio-e2e-tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS e2e TIMEOUT 120)
