# The public plugin SDK: a header-only C ABI, MIT licensed, usable without the rest of Mikser Studio.
add_library(mikserstudio-sdk INTERFACE)
add_library(MikserStudio::sdk ALIAS mikserstudio-sdk)
target_include_directories(mikserstudio-sdk INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(mikserstudio-sdk INTERFACE c_std_11)
