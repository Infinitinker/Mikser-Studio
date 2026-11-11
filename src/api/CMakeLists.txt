# MikserStudio::api, see docs/architecture.md and docs/api.md.
find_package(Qt6 6.8 REQUIRED COMPONENTS HttpServer Network WebSockets)

add_library(mikserstudio-api STATIC
    include/mikserstudio/api/ApiServer.h
    src/ApiResponse.cpp
    src/ApiResponse.h
    src/ApiServer.cpp
    src/Auth.cpp
    src/Auth.h
    src/DiscoveryFile.cpp
    src/DiscoveryFile.h
    src/EventHub.cpp
    src/EventHub.h
    src/HttpFrontend.cpp
    src/HttpFrontend.h
    src/Logging.cpp
    src/Logging.h
    src/OwnerOnlyPermissions.cpp
    src/OwnerOnlyPermissions.h
    src/Router.cpp
    src/Router.h
)
add_library(MikserStudio::api ALIAS mikserstudio-api)

target_include_directories(mikserstudio-api
    PUBLIC include
    PRIVATE src)
target_link_libraries(mikserstudio-api
    PUBLIC MikserStudio::core Qt6::Core
    PRIVATE Qt6::HttpServer Qt6::Network Qt6::WebSockets)
if(WIN32)
    target_link_libraries(mikserstudio-api PRIVATE advapi32)
endif()
# The public header uses std::expected.
target_compile_features(mikserstudio-api PUBLIC cxx_std_23)
target_compile_definitions(mikserstudio-api PRIVATE MIKSERSTUDIO_VERSION_STRING="${PROJECT_VERSION}")
mikserstudio_target_defaults(mikserstudio-api)
