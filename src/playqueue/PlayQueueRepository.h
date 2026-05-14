// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "PlayQueue.h"

#include <filesystem>

namespace vtplayer
{

/// Persistence for the single play queue. The file
/// (~/.config/ventty-player/playqueue.m3u) preserves the queue across runs;
/// vtplayer never exposes path selection or file management for it.
class PlayQueueRepository
{
public:
    /// Canonical path to playqueue.m3u under the user's config directory.
    static std::filesystem::path path();

    /// Ensure the parent directory exists. Safe to call repeatedly.
    static bool ensureDirectory();

    /// Load `playqueue.m3u` if it exists; otherwise create an empty one.
    static PlayQueue load();
};

} // namespace vtplayer
