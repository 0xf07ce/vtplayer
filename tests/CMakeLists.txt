# Unit tests for vtplayer's pure-logic modules (parsers, normalizer, play
# queue, config). These intentionally avoid the audio/library/UI layers so the
# test binary links nothing heavier than ventty (needed only for the UTF-8
# helper used by UnicodeNormalize).

add_executable(vtplayer_tests
    test_main.cpp
    test_util.cpp
    test_playqueue.cpp
    test_config.cpp

    # Modules under test, compiled directly into the test binary.
    ${CMAKE_SOURCE_DIR}/src/util/M3uReader.cpp
    ${CMAKE_SOURCE_DIR}/src/util/PlsReader.cpp
    ${CMAKE_SOURCE_DIR}/src/util/UnicodeNormalize.cpp
    ${CMAKE_SOURCE_DIR}/src/playqueue/PlayQueue.cpp
    ${CMAKE_SOURCE_DIR}/src/config/Config.cpp
)

target_include_directories(vtplayer_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ventty provides <ventty/core/Utf8.h> used by UnicodeNormalize.
target_link_libraries(vtplayer_tests PRIVATE ventty)

add_test(NAME vtplayer_tests COMMAND vtplayer_tests)
