# The Lua host's tests: plugins in isolated states, the sandbox, the limits, the ms API and hot reload.
add_executable(mikserstudio-lua-tests
    LuaHostTests.cpp
    LuaJsonTests.cpp
    LuaTestSupport.cpp
    LuaTestSupport.h
)
target_link_libraries(mikserstudio-lua-tests PRIVATE MikserStudio::lua MikserStudio::core Catch2::Catch2WithMain)
# The JSON conversion tests reach the host's private helpers.
target_include_directories(mikserstudio-lua-tests PRIVATE "${PROJECT_SOURCE_DIR}/src/lua/src")
target_link_libraries(mikserstudio-lua-tests PRIVATE mikserstudio-lua-sol2)
mikserstudio_target_defaults(mikserstudio-lua-tests)

catch_discover_tests(mikserstudio-lua-tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS lua)
