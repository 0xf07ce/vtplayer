// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace vtplayer
{

/// Sparse tag update. Only fields with a value set are written; others are
/// left untouched in the file's existing PropertyMap. Empty strings *are*
/// considered a value — they erase the corresponding key. Integer fields
/// with value 0 also erase the key (the scanner treats 0 as "unknown").
struct TagUpdate
{
    std::optional<std::string> title;
    std::optional<std::string> artist;
    std::optional<std::string> album;
    std::optional<std::string> albumArtist;
    std::optional<std::string> genre;
    std::optional<int>         trackNumber;
    std::optional<int>         discNumber;
    std::optional<int>         year;

    bool empty() const
    {
        return !title && !artist && !album && !albumArtist
               && !genre && !trackNumber && !discNumber && !year;
    }
};

/// Read the file's PropertyMap, apply the sparse update, and write it back.
/// Returns true if the file was written successfully. A failed save leaves
/// the on-disk file unchanged (TagLib's save() is the commit point).
bool applyTagUpdate(std::filesystem::path const & path, TagUpdate const & upd);

} // namespace vtplayer
