// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "test_framework.h"

#include "config/Config.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
std::filesystem::path writeIni(std::string const & content)
{
    static int counter = 0;
    auto dir = std::filesystem::temp_directory_path() / "vtplayer_tests";
    std::filesystem::create_directories(dir);
    auto path = dir / ("config_" + std::to_string(counter++) + ".ini");
    std::ofstream(path, std::ios::trunc) << content;
    return path;
}
} // namespace

using namespace vtplayer;

TEST_CASE("Config clamps out-of-range numeric values")
{
    auto path = writeIni("[audio]\n"
                         "stream_buffer_seconds = -5\n"
                         "stream_prebuffer_seconds = 9999\n"
                         "[visualizer]\n"
                         "bar_count = 100000\n");
    Config cfg;
    cfg.loadFrom(path);
    CHECK_EQ(cfg.streamBufferSeconds, 1.0f);     // clamped up from -5
    CHECK_EQ(cfg.streamPrebufferSeconds, 600.0f); // clamped down from 9999
    CHECK_EQ(cfg.barCount, 256);                  // clamped down from 100000
}

TEST_CASE("Config accepts in-range numeric values verbatim")
{
    auto path = writeIni("[audio]\n"
                         "stream_buffer_seconds = 30\n"
                         "[visualizer]\n"
                         "bar_count = 48\n");
    Config cfg;
    cfg.loadFrom(path);
    CHECK_EQ(cfg.streamBufferSeconds, 30.0f);
    CHECK_EQ(cfg.barCount, 48);
}

TEST_CASE("Config snaps visualizer fps to a supported tier")
{
    auto path = writeIni("[visualizer]\nfps = 50\n");
    Config cfg;
    cfg.loadFrom(path);
    CHECK_EQ(cfg.visualizerFps, 60); // 50 snaps up to 60 (> 45)
}

TEST_CASE("Config ignores a missing file and keeps defaults")
{
    Config cfg;
    cfg.loadFrom("/no/such/config.ini");
    CHECK_EQ(cfg.barCount, 24);              // struct default
    CHECK_EQ(cfg.streamBufferSeconds, 20.0f); // struct default
}

TEST_CASE("Config accepts source panel modes and legacy aliases")
{
    auto path = writeIni("[library]\nleft_mode = streaming\n");
    Config cfg;
    cfg.loadFrom(path);
    CHECK_EQ(cfg.leftMode, std::string("streaming"));

    path = writeIni("[library]\nleft_mode = files\n");
    Config files;
    files.loadFrom(path);
    CHECK_EQ(files.leftMode, std::string("files"));

    path = writeIni("[library]\nleft_mode = radio\n");
    Config legacy;
    legacy.loadFrom(path);
    CHECK_EQ(legacy.leftMode, std::string("radio"));
}
