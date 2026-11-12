# Warnings and sanitizers shared by every Mikser Studio target.
#
# Use mikserstudio_target_defaults(<target>) on each target we own; third-party code is untouched.
# Mikser Studio builds with free compilers only: GCC or Clang (MSYS2's UCRT64 on Windows), or Apple's
# Clang on macOS.

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR
        "Mikser Studio builds with GCC or Clang, but the C++ compiler is ${CMAKE_CXX_COMPILER_ID}. "
        "On Windows, configure from MSYS2's UCRT64 environment as docs/development.md describes.")
endif()

option(MIKSERSTUDIO_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(MIKSERSTUDIO_SANITIZERS "" CACHE STRING "Semicolon-separated sanitizers, e.g. address;undefined")

function(mikserstudio_target_defaults target)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast
        -Wcast-align -Woverloaded-virtual -Wconversion -Wsign-conversion
        -Wnull-dereference -Wimplicit-fallthrough)
    if(MIKSERSTUDIO_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
    if(MIKSERSTUDIO_SANITIZERS)
        list(JOIN MIKSERSTUDIO_SANITIZERS "," sanitizers)
        target_compile_options(${target} PRIVATE -fsanitize=${sanitizers} -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=${sanitizers})
    endif()
    target_compile_definitions(${target} PRIVATE
        QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII QT_NO_URL_CAST_FROM_STRING
        QT_NO_NARROWING_CONVERSIONS_IN_CONNECT QT_NO_KEYWORDS)
endfunction()
