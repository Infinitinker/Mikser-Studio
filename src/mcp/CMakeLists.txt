# mikser-studio-mcp, the MCP bridge (docs/api.md): a program of its own that drives the running app only
# through its local API. It links no other module of Mikser Studio.
find_package(Qt6 6.8 REQUIRED COMPONENTS Network WebSockets)

add_library(mikserstudio-mcp STATIC
    src/AppClient.cpp
    src/AppClient.h
    src/Bridge.cpp
    src/Bridge.h
    src/Discovery.cpp
    src/Discovery.h
    src/EventFollower.cpp
    src/EventFollower.h
    src/Logging.cpp
    src/Logging.h
    src/RecentEvents.cpp
    src/RecentEvents.h
    src/StandardStreams.cpp
    src/StandardStreams.h
    src/ToolList.cpp
    src/ToolList.h
)
add_library(MikserStudio::mcp ALIAS mikserstudio-mcp)

target_include_directories(mikserstudio-mcp PUBLIC src)
target_link_libraries(mikserstudio-mcp PUBLIC Qt6::Core Qt6::Network Qt6::WebSockets)
target_compile_features(mikserstudio-mcp PUBLIC cxx_std_23)
target_compile_definitions(mikserstudio-mcp PRIVATE MIKSERSTUDIO_VERSION_STRING="${PROJECT_VERSION}")
mikserstudio_target_defaults(mikserstudio-mcp)

# A console program on every system: its client talks to it on stdin and stdout.
add_executable(mikser-studio-mcp main.cpp)
target_link_libraries(mikser-studio-mcp PRIVATE MikserStudio::mcp)
target_compile_definitions(mikser-studio-mcp PRIVATE MIKSERSTUDIO_VERSION_STRING="${PROJECT_VERSION}")
mikserstudio_target_defaults(mikser-studio-mcp)

install(TARGETS mikser-studio-mcp RUNTIME DESTINATION bin)
