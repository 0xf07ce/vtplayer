// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace vtplayer
{

class MediaLibrary;
class LibraryRepository;

/// Walks a root directory, extracts audio metadata via TagLib, and reconciles
/// the result with the in-memory MediaLibrary and on-disk LibraryRepository.
/// Uses file mtime to skip unchanged files (incremental scan).
class LibraryScanner
{
public:
    LibraryScanner(MediaLibrary & library, LibraryRepository & repo);

    struct Result
    {
        int added   = 0;
        int updated = 0;
        int removed = 0;
        int skipped = 0; ///< unchanged (mtime match)
    };

    /// Recursively scans `root`, considering files whose lowercase extension
    /// matches one of `extensions` (entries are bare names like "mp3").
    /// Reads mtime via the filesystem entry; on read failure the file is
    /// scanned defensively (treated as new). No-op when `root` is empty or
    /// missing.
    Result scan(std::filesystem::path const & root,
                std::vector<std::string> const & extensions);

private:
    MediaLibrary &      _library;
    LibraryRepository & _repo;
};

} // namespace vtplayer
