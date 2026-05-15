// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "../audio/TrackInfo.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace vtplayer
{

/// Parser for Extended M3U (`.m3u` / `.m3u8`) files.
/// Recognized lines: `#EXTINF:<duration>,<artist> - <title>` followed by a path.
/// Other `#`-prefixed extension tags are ignored. Relative track paths are
/// resolved against the file's parent directory.
namespace M3uReader
{

/// Returns std::nullopt if the file cannot be opened. Tracks with unrecognized
/// formats are still returned (format = AudioFormat::Unknown).
std::optional<std::vector<TrackInfo>> read(std::filesystem::path const & path);

} // namespace M3uReader

} // namespace vtplayer
