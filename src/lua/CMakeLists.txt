# MikserStudio::lua, the Lua host: see docs/architecture.md and docs/plugins.md.
find_package(Lua 5.4 EXACT REQUIRED)

# Lua and sol2 behind one target. Their include directory is SYSTEM, so Lua's C macros stay out
# of our warnings.
add_library(mikserstudio-lua-sol2 INTERFACE)
target_include_directories(mikserstudio-lua-sol2 SYSTEM INTERFACE ${LUA_INCLUDE_DIR})
target_link_libraries(mikserstudio-lua-sol2 INTERFACE ${LUA_LIBRARIES})
target_compile_definitions(mikserstudio-lua-sol2 INTERFACE SOL_ALL_SAFETIES_ON=1)

# The prelude is compiled in, so the host can't run without it.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/prelude.lua" MIKSERSTUDIO_LUA_PRELUDE)
configure_file(src/Prelude.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/Prelude.h" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/prelude.lua")
# So are the types of the ms API that mikser-studio --print-lua-types writes.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/types.lua" MIKSERSTUDIO_LUA_TYPES)
configure_file(src/Types.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/Types.h" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/types.lua")

add_library(mikserstudio-lua STATIC
    include/mikserstudio/lua/LuaRuntime.h
    include/mikserstudio/lua/LuaTypes.h
    src/CommandSpecs.cpp
    src/CommandSpecs.h
    src/Logging.cpp
    src/Logging.h
    src/LuaJson.cpp
    src/LuaJson.h
    src/LuaPlugin.cpp
    src/LuaPlugin.h
    src/LuaRuntime.cpp
    src/LuaTypes.cpp
    src/LuaWorker.cpp
    src/LuaWorker.h
)
add_library(MikserStudio::lua ALIAS mikserstudio-lua)

target_include_directories(mikserstudio-lua
    PUBLIC include
    PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(mikserstudio-lua
    PUBLIC MikserStudio::core Qt6::Core
    PRIVATE mikserstudio-lua-sol2)
target_compile_features(mikserstudio-lua PUBLIC cxx_std_23)
mikserstudio_target_defaults(mikserstudio-lua)
