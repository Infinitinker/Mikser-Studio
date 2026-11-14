# The tests of the development scripts in tools/: plain Lua scripts, test_<script>.lua, run by Lua 5.4
# from the repository's root. Each is one test, tools.<script>, labeled `tools`, so the debug and ci test
# presets run them with the C++ tests.
find_program(MIKSERSTUDIO_LUA NAMES lua5.4 REQUIRED
    DOC "Lua 5.4, which runs the scripts in tools/ (docs/development.md)")

foreach(script IN ITEMS check_includes licenses)
    add_test(NAME "tools.${script}"
        COMMAND "${MIKSERSTUDIO_LUA}" "${CMAKE_CURRENT_SOURCE_DIR}/test_${script}.lua"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    set_tests_properties("tools.${script}" PROPERTIES LABELS tools)
endforeach()
