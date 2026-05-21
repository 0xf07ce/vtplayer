// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace vtplayer::StreamFile
{

/// Parsed contents of a `.stream` descriptor file.
///
/// Intentionally omits `artist` / `albumArtist` — streaming tracks are grouped
/// under the virtual `(stream)` artist node in LibraryView regardless of any
/// such tags. The descriptor file may still include those keys; the parser
/// silently drops them.
struct Meta
{
    std::string url;    ///< required; the only mandatory field
    std::string title;
    std::string album;
    std::string genre;
    int year = 0;
};

/// Parse a `.stream` INI file. Expects a `[stream]` section with at least a
/// `url` key. Returns nullopt when the file cannot be opened or `url` is
/// missing. Unknown keys are ignored.
std::optional<Meta> load(std::filesystem::path const & path);

} // namespace vtplayer::StreamFile
