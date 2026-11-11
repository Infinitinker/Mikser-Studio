# Example native plugin: the "mikserstudio.examples.hello.greet" command and the "Greeter" panel.
# It uses only the C ABI in sdk/, not Qt, so it does not depend on the Qt build of the host.
add_library(mikserstudio-plugin-hello MODULE
    src/FlatJson.cpp
    src/FlatJson.h
    src/HelloPlugin.cpp
)
target_link_libraries(mikserstudio-plugin-hello PRIVATE MikserStudio::sdk)
set_target_properties(mikserstudio-plugin-hello PROPERTIES
    OUTPUT_NAME hello
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    LIBRARY_OUTPUT_DIRECTORY "${MIKSERSTUDIO_PLUGIN_OUTPUT_DIRECTORY}/hello"
    RUNTIME_OUTPUT_DIRECTORY "${MIKSERSTUDIO_PLUGIN_OUTPUT_DIRECTORY}/hello"
    # Relink (and so re-run the copy below) when the manifest or the panel changes.
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/mikserstudio-plugin.json;${CMAKE_CURRENT_SOURCE_DIR}/hello.qml")
mikserstudio_target_defaults(mikserstudio-plugin-hello)

add_custom_command(TARGET mikserstudio-plugin-hello POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/mikserstudio-plugin.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/hello.qml"
        "$<TARGET_FILE_DIR:mikserstudio-plugin-hello>"
    COMMENT "Copying the hello plugin manifest and panel"
    VERBATIM)

install(TARGETS mikserstudio-plugin-hello
    LIBRARY DESTINATION bin/plugins/hello
    RUNTIME DESTINATION bin/plugins/hello)
install(FILES mikserstudio-plugin.json hello.qml DESTINATION bin/plugins/hello)
