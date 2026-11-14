cmake_minimum_required(VERSION 3.28)

# DESCRIPTION is the README's tagline and the line under it; mikser-studio --help shows it.
project(MikserStudio
    VERSION 0.1.0
    DESCRIPTION "A multimedia studio you share with AI agents. Create and edit video, sound, images and more, with the AI tools you choose, local or online."
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# The code uses no C++ modules: skip CMake's scan for them, whose GCC flags clang-tidy cannot read.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# A target `autogen` that runs Qt's moc, uic and rcc for every target, so clang-tidy can read the generated
# files without a full build ("C++ checks" in docs/development.md).
set(CMAKE_GLOBAL_AUTOGEN_TARGET ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(MikserStudioCompilerOptions)

# Every module can be switched off so it can be built and tested on its own.
# Dependencies between modules: core <- engine, api, services, lua <- ui <- app; mcp depends on none.
option(MIKSERSTUDIO_WITH_ENGINE "Build the playback engine and monitors (MLT)" ON)
option(MIKSERSTUDIO_WITH_LUA "Build the Lua host, which runs the plugins (Lua 5.4 and sol2)" ON)
option(MIKSERSTUDIO_WITH_API "Build the local /v1 API server" ON)
option(MIKSERSTUDIO_WITH_SERVICES "Build the services, the AI engine" ON)
option(MIKSERSTUDIO_WITH_UI "Build the desktop UI" ON)
option(MIKSERSTUDIO_WITH_MCP "Build the MCP bridge, mikser-studio-mcp, which uses only the local API" ON)
option(MIKSERSTUDIO_WITH_PLUGINS "Build the bundled example plugins" ON)
option(MIKSERSTUDIO_BUILD_TESTS "Build the test suite" ON)
option(MIKSERSTUDIO_INSTALLED_APP "Run as the installed Mikser Studio, not Mikser Studio Dev, when MIKSERSTUDIO_INSTANCE is unset. Only the package preset sets it (docs/releasing.md)." OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
qt_standard_project_setup(REQUIRES 6.8)

# Shared output folders so the app finds its plugins and test data at run time.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(MIKSERSTUDIO_PLUGIN_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins")

if(MIKSERSTUDIO_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 REQUIRED)
    include(Catch)
endif()

add_subdirectory(sdk)
add_subdirectory(src/core)

if(MIKSERSTUDIO_WITH_ENGINE)
    add_subdirectory(src/engine)
endif()
if(MIKSERSTUDIO_WITH_LUA)
    add_subdirectory(src/lua)
endif()
if(MIKSERSTUDIO_WITH_API)
    add_subdirectory(src/api)
endif()
if(MIKSERSTUDIO_WITH_SERVICES)
    add_subdirectory(src/services)
endif()
if(MIKSERSTUDIO_WITH_UI)
    add_subdirectory(src/ui)
endif()
if(MIKSERSTUDIO_WITH_MCP)
    add_subdirectory(src/mcp)
endif()
if(MIKSERSTUDIO_WITH_UI AND MIKSERSTUDIO_WITH_ENGINE AND MIKSERSTUDIO_WITH_API AND MIKSERSTUDIO_WITH_SERVICES)
    add_subdirectory(src/app)
endif()
if(MIKSERSTUDIO_WITH_PLUGINS)
    add_subdirectory(plugins)
endif()

if(MIKSERSTUDIO_BUILD_TESTS)
    add_subdirectory(tests)
    add_subdirectory(tools/tests)
endif()
