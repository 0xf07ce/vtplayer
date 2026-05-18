// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace vtplayer
{

class LibraryRepository;

/// Walks a root directory, extracts audio metadata via TagLib, and reconciles
/// the result with the on-disk LibraryRepository. Uses file mtime to skip
/// unchanged files (incremental scan).
///
/// Scanning is two phases the caller drives separately:
///   1. collect() — a single recursive filesystem walk that gathers the
///      matching files (path + mtime + size). Cheap (no TagLib), but I/O
///      bound; run it on the thread that owns the UI and pump input via the
///      tick callback so ESC stays responsive.
///   2. ingest() — reads tags for the collected files and writes the
///      LibraryRepository. Slow (one TagLib open per changed file); run it
///      on a background thread. Not cancellable.
///
/// ingest() touches only the repository, never the in-memory MediaLibrary,
/// so the UI thread can keep reading the pre-scan `MediaLibrary` snapshot
/// while a background ingest runs. Reload the library from the repository
/// once ingest() returns.
class LibraryScanner
{
public:
    explicit LibraryScanner(LibraryRepository & repo);

    /// A cheap (single stat) signature of the library root: its path plus
    /// last-write time. When this is unchanged since the last completed scan
    /// the caller may skip the walk entirely and trust the persisted index.
    /// Note this only reflects entries added/removed/renamed *directly* in
    /// the root directory — changes deeper in the tree need a manual rescan.
    /// Returns "" if `root` is empty or cannot be stat'd.
    static std::string rootSignature(std::filesystem::path const & root);

    /// One matching file discovered by collect().
    struct ScanEntry
    {
        std::filesystem::path path;
        std::int64_t          mtime = 0;
        std::int64_t          size  = 0;
    };

    struct Result
    {
        int added   = 0;
        int updated = 0;
        int removed = 0;
        int skipped = 0; ///< unchanged (mtime match)
    };

    /// Pass-1 tick, invoked roughly every 512 iterated directory entries with
    /// the running count of *collected* matching files. Return false to abort
    /// the walk (collect() then returns whatever it gathered so far and sets
    /// `canceled`). Called on the caller's thread — a good place to pump input
    /// and repaint.
    using CollectTickFn = std::function<bool(int collected)>;

    /// Pass-2 progress, invoked with an integer 0..100 percentage as files are
    /// ingested. Called on the caller's (background) thread.
    using IngestProgressFn = std::function<void(int percent)>;

    /// Polled by ingest() before each file. Returning true makes it stop
    /// early (leaving the repository partially updated and skipping the
    /// deletion sweep). Intended for shutdown teardown, not a user-facing
    /// mid-scan cancel.
    using StopFn = std::function<bool()>;

    /// Pass 1: recursively walk `root`, collecting files whose lowercase
    /// extension matches one of `extensions` (bare names like "mp3"). No-op
    /// returning an empty list when `root` is empty or missing. Sets
    /// `canceled` to true if `onTick` requested an abort.
    std::vector<ScanEntry> collect(std::filesystem::path const & root,
                                   std::vector<std::string> const & extensions,
                                   CollectTickFn const & onTick,
                                   bool & canceled);

    /// Pass 2: for each entry, skip files whose mtime matches the repository
    /// (incremental), otherwise read tags via TagLib and upsert into the
    /// repository. Finally sweep repository rows whose path was not in
    /// `entries` (deletions). Not cancellable.
    Result ingest(std::vector<ScanEntry> const & entries,
                  IngestProgressFn const & onProgress = {},
                  StopFn const & shouldStop = {});

private:
    LibraryRepository & _repo;
};

} // namespace vtplayer
