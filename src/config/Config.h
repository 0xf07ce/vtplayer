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
    bool gainNorm = false;  ///< master switch: prefer ReplayGain tag, fall back to runtime RMS
    /// Internet-radio ring-buffer depth and prebuffer/rebuffer threshold
    /// (seconds). Larger = more latency but more resilient to network
    /// jitter. prebuffer is clamped below the buffer depth at runtime.
    float streamBufferSeconds = 20.0f;
    float streamPrebufferSeconds = 5.0f;

    // [ui]
    bool showHidden = false;

    // [visualizer]
    int barCount = 24;
    int visualizerIndex = 1;  ///< 0=Oscilloscope, 1=Spectrum, 2=MatrixRain, 3=DebugBars, 4=TagInfo, 5=Vinyl
    /// Animation rate of the Visualizer screen. Clamped to 15/30/60 on load.
    /// Browser/Help screens use an idle-aware pacing regardless.
    int visualizerFps = 30;

    // [library]
    std::filesystem::path libraryRoot;
    /// Left-panel mode persisted across sessions: "files" | "directory" |
    /// "album" | "playlists" | "streaming". Legacy "artist" maps to album.
    std::string leftMode = "album";
    /// Absolute path of the track the library cursor rested on at exit.
    /// Restored (locate) on the next run so focus survives quitting — even
    /// when the run ended in FileBrowser (which normalizes to "album").
    std::filesystem::path libraryFocus;
    /// Library-root signature recorded after the last completed scan
    /// (LibraryScanner::rootSignature). Empty until the first scan. When it
    /// still matches on startup the filesystem walk is skipped entirely.
    std::string scanSig;

    // [keybindings]
    /// Active keybinding preset (file <preset>.keys under the keybindings dir).
    /// "default" = built-in keys; "vi" = modal, vi-style navigation.
    std::string keymapPreset = "default";

    // [theme] — color overrides as "#RRGGBB" hex strings
    std::unordered_map<std::string, std::string> themeColors;

    /// Load from default config path (~/.config/vtplayer/config.ini).
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
