# The Lua host's tests: plugins in isolated states, the sandbox, the limits, the ms API and hot reload.
add_executable(mikserstudio-lua-tests
    LuaHostTests.cpp
    LuaJobsTests.cpp
    LuaJsonTests.cpp
    LuaTestSupport.cpp
    LuaTestSupport.h
    LuaTypesTests.cpp
)
target_link_libraries(mikserstudio-lua-tests PRIVATE MikserStudio::lua MikserStudio::core Catch2::Catch2WithMain)
# The JSON conversion tests reach the host's private helpers.
target_include_directories(mikserstudio-lua-tests PRIVATE "${PROJECT_SOURCE_DIR}/src/lua/src")
target_link_libraries(mikserstudio-lua-tests PRIVATE mikserstudio-lua-sol2)
# The committed types, which the tests compare with what the app writes.
target_compile_definitions(mikserstudio-lua-tests PRIVATE
    MIKSERSTUDIO_LUA_TYPES_FILE="${PROJECT_SOURCE_DIR}/plugins/types/ms.lua")
mikserstudio_target_defaults(mikserstudio-lua-tests)

catch_discover_tests(mikserstudio-lua-tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS lua)
