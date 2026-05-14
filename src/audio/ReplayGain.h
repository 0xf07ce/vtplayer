// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <filesystem>

namespace vtplayer
{

struct ReplayGainInfo
{
    float trackGainDb = 0.0f; ///< dB; valid only when hasTrackGain
    float trackPeak   = 0.0f; ///< 0 means unknown
    bool  hasTrackGain = false;
};

/// Read REPLAYGAIN_TRACK_GAIN / REPLAYGAIN_TRACK_PEAK from the file using TagLib.
/// Returns default-constructed (hasTrackGain == false) if absent or unreadable.
ReplayGainInfo readReplayGain(std::filesystem::path const & path);

/// Dump every key/value pair in the file's TagLib PropertyMap to stdout.
/// Used by the `--dump-tags` CLI flag to inspect what a file actually exposes.
/// Returns false if the file cannot be opened.
bool dumpTags(std::filesystem::path const & path);

} // namespace vtplayer
