# The built-in plugins: every folder in builtin/ is one, staged as it is into
# ${MIKSERSTUDIO_PLUGIN_OUTPUT_DIRECTORY}/<name>/, the layout PluginManager discovers, and installed
# there. Lua plugins aren't compiled, so a new one needs no change outside its own folder. A plugin's
# spec/ folder holds its tests (tests/plugins), which are neither staged nor installed. The example in
# examples/ is never installed: run it with MIKSERSTUDIO_PLUGIN_PATH (docs/plugins.md).
file(GLOB folders LIST_DIRECTORIES true CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/builtin/*")
list(SORT folders)
set(staged)
foreach(folder IN LISTS folders)
    if(NOT IS_DIRECTORY "${folder}")
        continue()
    endif()
    get_filename_component(plugin "${folder}" NAME)
    file(GLOB_RECURSE files LIST_DIRECTORIES false CONFIGURE_DEPENDS "${folder}/*")
    foreach(file IN LISTS files)
        file(RELATIVE_PATH relative "${folder}" "${file}")
        if(relative MATCHES "^spec/")
            continue()
        endif()
        set(copy "${MIKSERSTUDIO_PLUGIN_OUTPUT_DIRECTORY}/${plugin}/${relative}")
        add_custom_command(OUTPUT "${copy}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${file}" "${copy}"
            DEPENDS "${file}"
            COMMENT "Staging plugins/builtin/${plugin}/${relative}"
            VERBATIM)
        list(APPEND staged "${copy}")
    endforeach()
    install(DIRECTORY "${folder}/" DESTINATION "bin/plugins/${plugin}" PATTERN "spec" EXCLUDE)
endforeach()
add_custom_target(mikserstudio-builtin-plugins ALL DEPENDS ${staged})
