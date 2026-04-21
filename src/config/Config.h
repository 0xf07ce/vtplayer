// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

namespace vtplayer
{

struct Config
{
    // [audio]
    float volume = 1.0f;
    bool autoGain = false;  ///< runtime RMS-based loudness normalization

    // [ui]
    std::filesystem::path startDirectory;
    bool showHidden = false;

    // [visualizer]
    int barCount = 48;

    // [formats]
    std::string extensions = "mp3,ogg,flac";

    // [theme] — color overrides as "#RRGGBB" hex strings
    std::unordered_map<std::string, std::string> themeColors;

    /// Load from default config path (~/.config/ventty-player/config.ini)
    void load();

    /// Load from a specific file
    void loadFrom(std::filesystem::path const & path);

    /// Get the default config file path
    static std::filesystem::path defaultPath();

private:
    void parseIni(std::string const & content);
    void applyValues(std::unordered_map<std::string, std::string> const & values);
};

} // namespace vtplayer
