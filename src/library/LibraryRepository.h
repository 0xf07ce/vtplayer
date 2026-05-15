// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "../audio/TrackInfo.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

struct sqlite3;
struct sqlite3_stmt;

namespace vtplayer
{

class MediaLibrary;

/// SQLite-backed persistence for the MediaLibrary index. The schema lives in
/// a single `tracks` table keyed by absolute path. Lifetime is RAII; closing
/// flushes any open prepared statements.
class LibraryRepository
{
public:
    /// Canonical path: `~/.config/ventty-player/library.db`.
    static std::filesystem::path defaultPath();

    explicit LibraryRepository(std::filesystem::path dbPath);
    ~LibraryRepository();

    LibraryRepository(LibraryRepository const &) = delete;
    LibraryRepository & operator=(LibraryRepository const &) = delete;

    /// Open the database (creating it and the schema if needed). Returns false
    /// on I/O failure or schema error.
    bool open();
    void close();

    bool isOpen() const { return _db != nullptr; }
    std::filesystem::path const & path() const { return _path; }

    /// Read every persisted row into `library`. Existing entries are overwritten.
    bool loadInto(MediaLibrary & library);

    bool upsert(TrackInfo const & track);
    bool erase(std::filesystem::path const & path);

    /// path → mtime map for incremental scans (compare against on-disk mtime).
    std::unordered_map<std::string, std::int64_t> mtimes() const;

private:
    bool ensureSchema();

    std::filesystem::path _path;
    sqlite3 *      _db          = nullptr;
    sqlite3_stmt * _upsertStmt  = nullptr;
    sqlite3_stmt * _eraseStmt   = nullptr;
};

} // namespace vtplayer
