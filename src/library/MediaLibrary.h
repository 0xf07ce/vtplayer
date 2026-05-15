// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "../audio/TrackInfo.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace vtplayer
{

/// In-memory index of all tracks under a registered root directory. The
/// scanner (LibraryScanner) populates it; the repository (LibraryRepository)
/// persists it to SQLite. Lookup by path is O(1); enumeration order matches
/// insertion (no implicit sort).
class MediaLibrary
{
public:
    void setRoot(std::filesystem::path root) { _root = std::move(root); }
    std::filesystem::path const & root() const { return _root; }

    int  size()  const { return static_cast<int>(_tracks.size()); }
    bool empty() const { return _tracks.empty(); }
    std::vector<TrackInfo> const & tracks() const { return _tracks; }

    /// Returns nullptr if no track with that path is indexed.
    TrackInfo const * find(std::filesystem::path const & path) const;

    void clear();

    /// Insert if absent, replace in place if a track with the same path exists.
    void upsert(TrackInfo track);

    /// No-op if the path is not indexed.
    void erase(std::filesystem::path const & path);

private:
    std::filesystem::path _root;
    std::vector<TrackInfo> _tracks;
    std::unordered_map<std::string, std::size_t> _byPath; ///< path string → index in _tracks
};

} // namespace vtplayer
