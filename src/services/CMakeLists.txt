# MikserStudio::services, see docs/architecture.md and docs/ai.md.
find_package(Qt6 6.8 REQUIRED COMPONENTS Network WebSockets)

add_library(mikserstudio-services STATIC
    include/mikserstudio/services/AiCommands.h
    include/mikserstudio/services/AiEngine.h
    include/mikserstudio/services/AiSettings.h
    include/mikserstudio/services/AiTypes.h
    include/mikserstudio/services/HttpJobs.h
    src/AiBackend.h
    src/AiCommands.cpp
    src/AiEngine.cpp
    src/AiJobMirror.cpp
    src/AiResultImport.cpp
    src/AiResultImport.h
    src/AiSettings.cpp
    src/AiTypes.cpp
    src/ComfyUiBackend.cpp
    src/ComfyUiBackend.h
    src/ComfyUiWorkflow.cpp
    src/ComfyUiWorkflow.h
    src/HttpJobs.cpp
    src/HttpRequests.cpp
    src/HttpRequests.h
    src/Logging.cpp
    src/Logging.h
    src/NetworkAccess.cpp
    src/NetworkAccess.h
)
add_library(MikserStudio::services ALIAS mikserstudio-services)

target_include_directories(mikserstudio-services
    PUBLIC include
    PRIVATE src)
target_link_libraries(mikserstudio-services PUBLIC MikserStudio::core Qt6::Core Qt6::Network PRIVATE Qt6::WebSockets)
# The public headers use std::expected.
target_compile_features(mikserstudio-services PUBLIC cxx_std_23)
# The provenance of AI results names the app's version.
target_compile_definitions(mikserstudio-services PRIVATE MIKSERSTUDIO_VERSION_STRING="${PROJECT_VERSION}")
mikserstudio_target_defaults(mikserstudio-services)
