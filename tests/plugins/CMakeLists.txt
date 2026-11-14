# Tests of the plugins, one folder per plugin; a plugin without a folder here has no tests.
file(GLOB folders LIST_DIRECTORIES true CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")
list(SORT folders)
foreach(folder IN LISTS folders)
    if(IS_DIRECTORY "${folder}" AND EXISTS "${folder}/CMakeLists.txt")
        add_subdirectory("${folder}")
    endif()
endforeach()
