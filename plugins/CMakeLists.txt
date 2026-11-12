# The bundled plugins: every folder here with a CMakeLists.txt, so a new plugin needs no change outside
# its own folder. Each one is built into ${MIKSERSTUDIO_PLUGIN_OUTPUT_DIRECTORY}/<name>/ next to its
# mikserstudio-plugin.json, the layout PluginManager discovers.
file(GLOB folders LIST_DIRECTORIES true CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*")
list(SORT folders)
foreach(folder IN LISTS folders)
    if(IS_DIRECTORY "${folder}" AND EXISTS "${folder}/CMakeLists.txt")
        add_subdirectory("${folder}")
    endif()
endforeach()
