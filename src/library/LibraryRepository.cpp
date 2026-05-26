// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "LibraryRepository.h"

#include "MediaLibrary.h"

#include <sqlite3.h>

#include <cstdlib>
#include <system_error>
#include <utility>

namespace vtplayer
{

namespace
{

constexpr char const * kSchema = R"(
CREATE TABLE IF NOT EXISTS tracks (
    path         TEXT PRIMARY KEY,
    title        TEXT NOT NULL DEFAULT '',
    artist       TEXT NOT NULL DEFAULT '',
    album        TEXT NOT NULL DEFAULT '',
    album_artist TEXT NOT NULL DEFAULT '',
    genre        TEXT NOT NULL DEFAULT '',
    track_no     INTEGER NOT NULL DEFAULT 0,
    disc_no      INTEGER NOT NULL DEFAULT 0,
    year         INTEGER NOT NULL DEFAULT 0,
    duration     REAL    NOT NULL DEFAULT 0,
    format       INTEGER NOT NULL DEFAULT 0,
    mtime        INTEGER NOT NULL DEFAULT 0,
    size         INTEGER NOT NULL DEFAULT 0,
    stream_url   TEXT NOT NULL DEFAULT '',
    grouping     TEXT NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS idx_tracks_album_artist
    ON tracks(album_artist, album, disc_no, track_no);
)";

constexpr char const * kUpsertSql = R"(
INSERT INTO tracks
    (path, title, artist, album, album_artist, genre,
     track_no, disc_no, year, duration, format, mtime, size, stream_url, grouping)
VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
ON CONFLICT(path) DO UPDATE SET
    title        = excluded.title,
    artist       = excluded.artist,
    album        = excluded.album,
    album_artist = excluded.album_artist,
    genre        = excluded.genre,
    track_no     = excluded.track_no,
    disc_no      = excluded.disc_no,
    year         = excluded.year,
    duration     = excluded.duration,
    format       = excluded.format,
    mtime        = excluded.mtime,
    size         = excluded.size,
    stream_url   = excluded.stream_url,
    grouping     = excluded.grouping;
)";

constexpr char const * kEraseSql = "DELETE FROM tracks WHERE path = ?;";

void bindText(sqlite3_stmt * stmt, int idx, std::string const & value)
{
    sqlite3_bind_text(stmt, idx, value.c_str(),
                      static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

std::string columnText(sqlite3_stmt * stmt, int idx)
{
    char const * raw = reinterpret_cast<char const *>(sqlite3_column_text(stmt, idx));
    return raw ? std::string(raw) : std::string();
}

} // namespace

std::filesystem::path LibraryRepository::defaultPath()
{
    char const * home = std::getenv("HOME");
    if (!home) return {};
    return std::filesystem::path(home) / ".config" / "vtplayer" / "library.db";
}

LibraryRepository::LibraryRepository(std::filesystem::path dbPath)
    : _path(std::move(dbPath))
{
}

LibraryRepository::~LibraryRepository()
{
    close();
}

bool LibraryRepository::open()
{
    if (_db) return true;
    if (_path.empty()) return false;

    std::error_code ec;
    if (auto parent = _path.parent_path(); !parent.empty())
    {
        std::filesystem::create_directories(parent, ec);
        if (ec) return false;
    }

    if (sqlite3_open(_path.string().c_str(), &_db) != SQLITE_OK)
    {
        close();
        return false;
    }

    // Reasonable defaults for a single-process desktop app.
    sqlite3_exec(_db, "PRAGMA journal_mode=WAL;",   nullptr, nullptr, nullptr);
    sqlite3_exec(_db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    if (!ensureSchema())
    {
        close();
        return false;
    }

    if (sqlite3_prepare_v2(_db, kUpsertSql, -1, &_upsertStmt, nullptr) != SQLITE_OK ||
        sqlite3_prepare_v2(_db, kEraseSql,  -1, &_eraseStmt,  nullptr) != SQLITE_OK)
    {
        close();
        return false;
    }
    return true;
}

void LibraryRepository::close()
{
    if (_upsertStmt) { sqlite3_finalize(_upsertStmt); _upsertStmt = nullptr; }
    if (_eraseStmt)  { sqlite3_finalize(_eraseStmt);  _eraseStmt  = nullptr; }
    if (_db)         { sqlite3_close(_db);            _db         = nullptr; }
}

bool LibraryRepository::ensureSchema()
{
    char * err = nullptr;
    int const rc = sqlite3_exec(_db, kSchema, nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
    if (rc != SQLITE_OK) return false;

    // Idempotent migration for DBs created before stream_url existed. Failure
    // (duplicate column) is expected on freshly-created or already-migrated
    // schemas — sqlite has no IF NOT EXISTS for ADD COLUMN, so we just try
    // and discard the error.
    sqlite3_exec(_db,
                 "ALTER TABLE tracks ADD COLUMN stream_url TEXT NOT NULL DEFAULT ''",
                 nullptr, nullptr, nullptr);

    // Schema versioning starts at 1 with the grouping column. Pre-versioned
    // DBs report user_version=0, get the column added, and have their mtimes
    // wiped so the next scan re-reads the file's GROUPING tag instead of
    // skipping every entry as "unchanged".
    int userVersion = 0;
    sqlite3_stmt * vs = nullptr;
    if (sqlite3_prepare_v2(_db, "PRAGMA user_version;", -1, &vs, nullptr) == SQLITE_OK)
    {
        if (sqlite3_step(vs) == SQLITE_ROW)
            userVersion = sqlite3_column_int(vs, 0);
        sqlite3_finalize(vs);
    }
    if (userVersion < 1)
    {
        sqlite3_exec(_db,
                     "ALTER TABLE tracks ADD COLUMN grouping TEXT NOT NULL DEFAULT ''",
                     nullptr, nullptr, nullptr);
        sqlite3_exec(_db, "UPDATE tracks SET mtime = 0;", nullptr, nullptr, nullptr);
        sqlite3_exec(_db, "PRAGMA user_version = 1;", nullptr, nullptr, nullptr);
    }
    return true;
}

bool LibraryRepository::loadInto(MediaLibrary & library)
{
    if (!_db) return false;

    constexpr char const * kSelect = R"(
SELECT path, title, artist, album, album_artist, genre,
       track_no, disc_no, year, duration, format, mtime, size, stream_url, grouping
FROM tracks;
)";

    sqlite3_stmt * stmt = nullptr;
    if (sqlite3_prepare_v2(_db, kSelect, -1, &stmt, nullptr) != SQLITE_OK)
    {
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        TrackInfo info;
        info.path        = columnText(stmt, 0);
        info.title       = columnText(stmt, 1);
        info.artist      = columnText(stmt, 2);
        info.album       = columnText(stmt, 3);
        info.albumArtist = columnText(stmt, 4);
        info.genre       = columnText(stmt, 5);
        info.trackNumber = sqlite3_column_int(stmt, 6);
        info.discNumber  = sqlite3_column_int(stmt, 7);
        info.year        = sqlite3_column_int(stmt, 8);
        info.duration    = static_cast<float>(sqlite3_column_double(stmt, 9));
        info.format      = static_cast<AudioFormat>(sqlite3_column_int(stmt, 10));
        info.mtime       = sqlite3_column_int64(stmt, 11);
        info.size        = sqlite3_column_int64(stmt, 12);
        info.streamUrl   = columnText(stmt, 13);
        info.grouping    = columnText(stmt, 14);
        library.upsert(std::move(info));
    }

    sqlite3_finalize(stmt);
    return true;
}

bool LibraryRepository::upsert(TrackInfo const & track)
{
    if (!_db || !_upsertStmt) return false;

    sqlite3_reset(_upsertStmt);
    sqlite3_clear_bindings(_upsertStmt);

    bindText(_upsertStmt, 1, track.path.string());
    bindText(_upsertStmt, 2, track.title);
    bindText(_upsertStmt, 3, track.artist);
    bindText(_upsertStmt, 4, track.album);
    bindText(_upsertStmt, 5, track.albumArtist);
    bindText(_upsertStmt, 6, track.genre);
    sqlite3_bind_int   (_upsertStmt, 7,  track.trackNumber);
    sqlite3_bind_int   (_upsertStmt, 8,  track.discNumber);
    sqlite3_bind_int   (_upsertStmt, 9,  track.year);
    sqlite3_bind_double(_upsertStmt, 10, track.duration);
    sqlite3_bind_int   (_upsertStmt, 11, static_cast<int>(track.format));
    sqlite3_bind_int64 (_upsertStmt, 12, track.mtime);
    sqlite3_bind_int64 (_upsertStmt, 13, track.size);
    bindText           (_upsertStmt, 14, track.streamUrl);
    bindText           (_upsertStmt, 15, track.grouping);

    return sqlite3_step(_upsertStmt) == SQLITE_DONE;
}

bool LibraryRepository::erase(std::filesystem::path const & path)
{
    if (!_db || !_eraseStmt) return false;

    sqlite3_reset(_eraseStmt);
    sqlite3_clear_bindings(_eraseStmt);
    bindText(_eraseStmt, 1, path.string());
    return sqlite3_step(_eraseStmt) == SQLITE_DONE;
}

bool LibraryRepository::clear()
{
    if (!_db) return false;
    char * err = nullptr;
    int const rc = sqlite3_exec(_db, "DELETE FROM tracks;", nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
    return rc == SQLITE_OK;
}

std::unordered_map<std::string, std::int64_t> LibraryRepository::mtimes() const
{
    std::unordered_map<std::string, std::int64_t> out;
    if (!_db) return out;

    sqlite3_stmt * stmt = nullptr;
    if (sqlite3_prepare_v2(_db, "SELECT path, mtime FROM tracks;", -1, &stmt, nullptr) != SQLITE_OK)
    {
        return out;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.emplace(columnText(stmt, 0), sqlite3_column_int64(stmt, 1));
    }
    sqlite3_finalize(stmt);
    return out;
}

} // namespace vtplayer
