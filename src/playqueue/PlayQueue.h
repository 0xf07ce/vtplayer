// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "../audio/TrackInfo.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vtplayer
{

/// In-memory play queue backed by an Extended M3U file on disk.
/// File format: UTF-8, lines of `#EXTINF:<duration>,<artist> - <title>` followed by a path.
class PlayQueue
{
public:
    PlayQueue() = default;
    explicit PlayQueue(std::filesystem::path path) : _path(std::move(path)) {}

    /// Parse an M3U/M3U8 file. Relative track paths are resolved against the file's directory.
    /// Returns std::nullopt if the file cannot be opened.
    static std::optional<PlayQueue> load(std::filesystem::path const & path);

    /// Write the current tracks to `_path`. Parent directories are created as needed.
    bool save() const;

    std::filesystem::path const & path() const { return _path; }
    void setPath(std::filesystem::path path) { _path = std::move(path); }

    /// Stem of the file (e.g. "default" for "default.m3u"). NFC-normalized.
    std::string name() const;

    std::vector<TrackInfo> const & tracks() const { return _tracks; }
    void setTracks(std::vector<TrackInfo> tracks) { _tracks = std::move(tracks); }

private:
    std::filesystem::path _path;
    std::vector<TrackInfo> _tracks;
};

} // namespace vtplayer
