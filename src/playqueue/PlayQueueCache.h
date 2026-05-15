// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "../audio/TrackInfo.h"

#include <filesystem>
#include <vector>

namespace vtplayer
{

class MediaLibrary;

/// Session persistence for the volatile PlayQueue. The queue itself is
/// in-memory only; on shutdown we snapshot the list of absolute paths to a
/// plain-text cache file, and on startup we re-materialize TrackInfo by
/// looking each path up in the MediaLibrary index. Paths the library doesn't
/// know about (e.g. files outside the library root) are kept as minimal
/// TrackInfo records so external imports survive a restart.
namespace PlayQueueCache
{

/// Canonical path: `~/.config/ventty-player/playqueue.cache`.
std::filesystem::path defaultPath();

/// Write the path list. Truncates any existing cache. Returns false on I/O
/// failure or when `defaultPath()` is empty (no $HOME).
bool save(std::vector<TrackInfo> const & tracks);

/// Read the path list and resolve each entry against `library`. Returns an
/// empty vector when the cache is missing.
std::vector<TrackInfo> restore(MediaLibrary const & library);

} // namespace PlayQueueCache

} // namespace vtplayer
