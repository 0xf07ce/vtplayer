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
    bool gainNorm = false;  ///< master switch: prefer ReplayGain tag, fall back to runtime RMS

    // [ui]
    bool showHidden = false;

    // [visualizer]
    int barCount = 48;
    int visualizerIndex = 1;  ///< 0=Oscilloscope, 1=Spectrum, 2=MatrixRain, 3=DebugBars, 4=TagInfo, 5=Vinyl

    // [formats]
    std::string extensions = "mp3,wav,ogg,flac";

    // [library]
    std::filesystem::path libraryRoot;
    /// Left-panel mode persisted across sessions: "artist" | "album" |
    /// "directory". The transient "filebrowser" mode is never persisted —
    /// it normalizes back to "album" on save so a fresh run starts in the
    /// indexed library.
    std::string leftMode = "album";

    // [theme] — color overrides as "#RRGGBB" hex strings
    std::unordered_map<std::string, std::string> themeColors;

    /// Load from default config path (~/.config/ventty-player/config.ini).
    /// If the file does not exist, writes out the current values as defaults.
    void load();

    /// Load from a specific file
    void loadFrom(std::filesystem::path const & path);

    /// Save current values to the default config path, creating parent dirs.
    bool save() const;

    /// Save current values to a specific file
    bool saveTo(std::filesystem::path const & path) const;

    /// Get the default config file path
    static std::filesystem::path defaultPath();

private:
    void parseIni(std::string const & content);
    void applyValues(std::unordered_map<std::string, std::string> const & values);
    std::string serializeIni() const;
};

} // namespace vtplayer
