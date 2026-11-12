# Example native plugin: the "mikserstudio.examples.hello.greet" command and the "Greeter" panel.
# It uses only the C ABI in sdk/, not Qt, so it does not depend on the Qt build of the host.
#
# Every name here comes from the folder's name, so a copy of this folder under another name builds as a
# plugin of its own: the library is <folder>.dll (the manifest's "entry"), built into
# bin/plugins/<folder>/ with the manifest and every QML file next to it, and installed there.
get_filename_component(plugin "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
set(target "mikserstudio-plugin-${plugin}")
file(GLOB sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
file(GLOB panels CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.qml")
set(manifest "${CMAKE_CURRENT_SOURCE_DIR}/mikserstudio-plugin.json")

add_library(${target} MODULE ${sources})
target_link_libraries(${target} PRIVATE MikserStudio::sdk)
set_target_properties(${target} PROPERTIES
    OUTPUT_NAME "${plugin}"
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    LIBRARY_OUTPUT_DIRECTORY "${MIKSERSTUDIO_PLUGIN_OUTPUT_DIRECTORY}/${plugin}"
    RUNTIME_OUTPUT_DIRECTORY "${MIKSERSTUDIO_PLUGIN_OUTPUT_DIRECTORY}/${plugin}"
    # Relink (and so re-run the copy below) when the manifest or a panel changes.
    LINK_DEPENDS "${manifest};${panels}")
mikserstudio_target_defaults(${target})

add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${manifest}" ${panels} "$<TARGET_FILE_DIR:${target}>"
    COMMENT "Copying the ${plugin} plugin's manifest and panels"
    VERBATIM)

install(TARGETS ${target}
    LIBRARY DESTINATION "bin/plugins/${plugin}"
    RUNTIME DESTINATION "bin/plugins/${plugin}")
install(FILES "${manifest}" ${panels} DESTINATION "bin/plugins/${plugin}")
