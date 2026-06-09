# Unit tests for vtplayer's pure-logic modules (parsers, normalizer, play
# queue, config). These intentionally avoid the audio/library/UI layers so the
# test binary links nothing heavier than ventty (needed only for the UTF-8
# helper used by UnicodeNormalize).

# The dummy plugins are pure C — proving the host boundary is a real C ABI.
enable_language(C)

# Build the dummy plugin modules first; the test loads them at runtime.
add_library(vtp_test_dummy_input MODULE plugins/dummy_input.c)
add_library(vtp_test_bad_abi     MODULE plugins/bad_abi.c)
foreach(mod vtp_test_dummy_input vtp_test_bad_abi)
    set_target_properties(${mod} PROPERTIES
        PREFIX ""
        C_VISIBILITY_PRESET hidden
        POSITION_INDEPENDENT_CODE ON)
    target_include_directories(${mod} PRIVATE ${CMAKE_SOURCE_DIR}/include)
endforeach()

add_executable(vtplayer_tests
    test_main.cpp
    test_util.cpp
    test_playqueue.cpp
    test_config.cpp
    test_plugin.cpp
    test_keybindings.cpp

    # Modules under test, compiled directly into the test binary.
    ${CMAKE_SOURCE_DIR}/src/util/M3uReader.cpp
    ${CMAKE_SOURCE_DIR}/src/util/PlsReader.cpp
    ${CMAKE_SOURCE_DIR}/src/util/UnicodeNormalize.cpp
    ${CMAKE_SOURCE_DIR}/src/playqueue/PlayQueue.cpp
    ${CMAKE_SOURCE_DIR}/src/config/Config.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/PluginHost.cpp
    ${CMAKE_SOURCE_DIR}/src/plugin/DecoderRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/audio/PluginSource.cpp
    ${CMAKE_SOURCE_DIR}/src/input/Actions.cpp
    ${CMAKE_SOURCE_DIR}/src/input/Keybindings.cpp
)

target_include_directories(vtplayer_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ventty provides <ventty/core/Utf8.h> used by UnicodeNormalize; ${CMAKE_DL_LIBS}
# is libdl on Linux (dlopen), empty on macOS.
target_link_libraries(vtplayer_tests PRIVATE ventty ${CMAKE_DL_LIBS})

# Point the test at the freshly-built plugin modules and ensure they exist.
add_dependencies(vtplayer_tests vtp_test_dummy_input vtp_test_bad_abi)
target_compile_definitions(vtplayer_tests PRIVATE
    DUMMY_PLUGIN_DIR="$<TARGET_FILE_DIR:vtp_test_dummy_input>")

add_test(NAME vtplayer_tests COMMAND vtplayer_tests)
