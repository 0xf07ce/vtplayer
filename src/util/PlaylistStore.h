// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vtplayer
{

/// Manages the on-disk playlist collection: a fixed directory of individual
/// Extended-M3U files (`<name>.m3u8`) under ~/.config/vtplayer/playlists/.
/// The directory is not configurable. The store owns no UI state; it only
/// lists / creates / deletes files. Track contents are read back later via
/// the existing M3uReader, so a playlist is just a flat, human-editable file.
class PlaylistStore
{
public:
    /// ~/.config/vtplayer/playlists. Empty path if $HOME is unset.
    static std::filesystem::path defaultDir();

    explicit PlaylistStore(std::filesystem::path dir = defaultDir());

    std::filesystem::path const & dir() const { return _dir; }

    /// Display names (file stems) of every `.m3u8` in the directory, sorted
    /// case-insensitively. A missing directory yields an empty list (never
    /// an error).
    std::vector<std::string> list() const;

    /// Create an empty playlist. The name is sanitized to a safe filename;
    /// returns the sanitized display name on success, or nullopt if the name
    /// is empty/invalid after sanitization or a playlist by that name already
    /// exists.
    std::optional<std::string> create(std::string const & name);

    /// Rename `<oldName>.m3u8` to `<newName>.m3u8`. The new name is sanitized
    /// like create(); returns the sanitized new name on success, or nullopt if
    /// it is empty/invalid, the source is missing, or a playlist by the new
    /// name already exists (collision). Renaming to the same name is a no-op
    /// success.
    std::optional<std::string> rename(std::string const & oldName,
                                      std::string const & newName);

    /// Delete `<name>.m3u8`. Returns true if a file was removed.
    bool remove(std::string const & name);

    /// Absolute path of the file backing the given playlist name.
    std::filesystem::path pathFor(std::string const & name) const;

private:
    std::filesystem::path _dir;
};

} // namespace vtplayer
