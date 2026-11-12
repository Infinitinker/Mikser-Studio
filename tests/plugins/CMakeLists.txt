# Tests of the bundled plugins, one folder per plugin as in plugins/. Each loads its plugin through the
# core's PluginManager, as the app does; a plugin without a folder here has no tests.
file(GLOB folders LIST_DIRECTORIES true CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")
list(SORT folders)
foreach(folder IN LISTS folders)
    get_filename_component(plugin "${folder}" NAME)
    if(IS_DIRECTORY "${folder}" AND EXISTS "${folder}/CMakeLists.txt" AND TARGET mikserstudio-plugin-${plugin})
        add_subdirectory("${folder}")
    endif()
endforeach()
