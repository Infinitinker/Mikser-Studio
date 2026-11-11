# MikserStudio::services, see docs/architecture.md and docs/ai.md.
find_package(Qt6 6.8 REQUIRED COMPONENTS Network)

add_library(mikserstudio-services STATIC
    include/mikserstudio/services/AiClient.h
    include/mikserstudio/services/AiCommands.h
    include/mikserstudio/services/AiTypes.h
    include/mikserstudio/services/SidecarManager.h
    src/AiClient.cpp
    src/AiCommands.cpp
    src/AiJobTracker.cpp
    src/AiJobTracker.h
    src/AiJson.cpp
    src/AiJson.h
    src/AiTypes.cpp
    src/Logging.cpp
    src/Logging.h
    src/SidecarLaunch.cpp
    src/SidecarLaunch.h
    src/SidecarManager.cpp
    src/UpscalePlacement.cpp
    src/UpscalePlacement.h
)
add_library(MikserStudio::services ALIAS mikserstudio-services)

target_include_directories(mikserstudio-services
    PUBLIC include
    PRIVATE src)
target_link_libraries(mikserstudio-services PUBLIC MikserStudio::core Qt6::Core Qt6::Network)
# The public headers use std::expected.
target_compile_features(mikserstudio-services PUBLIC cxx_std_23)
mikserstudio_target_defaults(mikserstudio-services)

