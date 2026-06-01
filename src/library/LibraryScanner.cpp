// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "LibraryScanner.h"

#include "LibraryRepository.h"
#include "../plugin/DecoderRegistry.h"
#include "../util/PlsReader.h"
#include "../util/UnicodeNormalize.h"

#include "vtplayer/plugin.h"

// Bare header names — see audio/ReplayGain.cpp for rationale.
#include <audioproperties.h>
#include <fileref.h>
#include <tfile.h>
#include <tpropertymap.h>
#include <tstring.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <optional>
#include <system_error>
#include <unordered_set>

namespace vtplayer
{

namespace
{

namespace fs = std::filesystem;

std::string lowerExt(fs::path const & p)
{
    std::string ext = p.extension().string();
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
    for (auto & c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

/// Convert a filesystem time point to seconds since the unix epoch in a way
/// that works on libc++ versions without `clock_cast` (notably AppleClang).
std::int64_t toUnixSeconds(fs::file_time_type t)
{
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count());
}

std::string firstValue(TagLib::PropertyMap const & props, char const * key)
{
    TagLib::String const wanted = TagLib::String(key).upper();
    for (auto const & entry : props)
    {
        if (entry.first.upper() == wanted && !entry.second.isEmpty())
        {
            return entry.second.front().to8Bit(/*unicode=*/true);
        }
    }
    return {};
}

int parseLeadingInt(std::string const & v)
{
    // TRACKNUMBER may be "5" or "5/12"; DATE may be "1998" or "1998-04-12".
    try
    {
        if (v.empty()) return 0;
        return std::stoi(v);
    }
    catch (...)
    {
        return 0;
    }
}

TrackInfo extractMetadata(fs::path const & path)
{
    TrackInfo info;
    info.path   = path;
    info.format = TrackInfo::formatFromPath(path);
    info.title  = toNfc(path.stem().string()); // fallback if no tag

    // Plugin-owned formats (VGM, ROL, …) are not TagLib formats. Ask the
    // owning input plugin for metadata first; if it has no read_tags or it
    // fails, the filename-stem fallback above stands. Either way TagLib is
    // skipped — opening a chip-tune file through TagLib would just fail.
    if (VtpInputPlugin const * plug = DecoderRegistry::instance().find(lowerExt(path)))
    {
        info.format = AudioFormat::Plugin;
        if (plug->read_tags)
        {
            VtpTagOut t{};
            t.struct_size = sizeof(t);
            if (plug->read_tags(path.string().c_str(), &t) == 0)
            {
                if (t.title[0])    info.title    = toNfc(t.title);
                if (t.artist[0])   info.artist   = toNfc(t.artist);
                if (t.album[0])    info.album    = toNfc(t.album);
                if (t.grouping[0]) info.grouping = toNfc(t.grouping);
                info.trackNumber = t.track_number;
                info.year        = t.year;
                if (t.duration > 0.0)
                    info.duration = static_cast<float>(t.duration);
            }
        }
        return info;
    }

    TagLib::FileRef ref(path.c_str());
    if (ref.isNull() || !ref.file())
    {
        return info;
    }

    TagLib::PropertyMap props = ref.file()->properties();

    auto title = firstValue(props, "TITLE");
    if (!title.empty()) info.title = toNfc(title);

    info.artist      = toNfc(firstValue(props, "ARTIST"));
    info.album       = toNfc(firstValue(props, "ALBUM"));
    info.albumArtist = toNfc(firstValue(props, "ALBUMARTIST"));
    info.genre       = toNfc(firstValue(props, "GENRE"));
    // ID3v2 TIT1 / Vorbis GROUPING / MP4 ©grp — TagLib normalizes them all
    // to the PropertyMap key "GROUPING". Used as the top-level tree axis.
    info.grouping    = toNfc(firstValue(props, "GROUPING"));
    info.trackNumber = parseLeadingInt(firstValue(props, "TRACKNUMBER"));
    info.discNumber  = parseLeadingInt(firstValue(props, "DISCNUMBER"));
    info.year        = parseLeadingInt(firstValue(props, "DATE"));

    if (auto * ap = ref.audioProperties())
    {
        info.duration = static_cast<float>(ap->lengthInSeconds());
    }
    return info;
}

} // namespace

LibraryScanner::LibraryScanner(LibraryRepository & repo)
    : _repo(repo)
{
}

std::string LibraryScanner::rootSignature(fs::path const & root)
{
    if (root.empty()) return {};
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return {};
    auto t = fs::last_write_time(root, ec);
    if (ec) return {};
    return root.string() + "|" + std::to_string(toUnixSeconds(t));
}

std::vector<LibraryScanner::ScanEntry>
LibraryScanner::collect(fs::path const & root,
                        std::vector<std::string> const & extensions,
                        CollectTickFn const & onTick,
                        bool & canceled)
{
    canceled = false;
    std::vector<ScanEntry> entries;

    if (root.empty()) return entries;

    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) return entries;

    std::unordered_set<std::string> extSet;
    for (auto e : extensions)
    {
        for (auto & c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!e.empty() && e[0] == '.') e.erase(0, 1);
        if (!e.empty()) extSet.insert(std::move(e));
    }
    // `.pls` playlists are always collected, regardless of the user's
    // `[formats] extensions` setting — they aren't audio containers and
    // shouldn't have to be listed there.
    extSet.insert("pls");

    // Extensions claimed by loaded input plugins are always collected so
    // plugin-handled formats (VGM, ROL, …) get indexed without the user
    // having to add them to `[formats] extensions`.
    for (auto & e : DecoderRegistry::instance().extensions())
        extSet.insert(std::move(e));

    // Tick cadence in *iterated* directory entries (not just matching files)
    // so ESC stays responsive even inside subtrees that contain no audio.
    constexpr int kTickEvery = 512;

    auto tick = [&]() -> bool
    {
        if (!onTick) return true;
        return onTick(static_cast<int>(entries.size()));
    };

    if (!tick()) { canceled = true; return entries; }

    int iterated = 0;
    fs::recursive_directory_iterator it(
        root, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec))
    {
        if (ec) { ec.clear(); continue; }

        if (++iterated % kTickEvery == 0)
        {
            if (!tick()) { canceled = true; return entries; }
        }

        auto const & entry = *it;
        if (!entry.is_regular_file(ec)) { ec.clear(); continue; }
        auto const & p = entry.path();
        if (extSet.find(lowerExt(p)) == extSet.end()) continue;

        ScanEntry se;
        se.path = p;
        if (auto t = entry.last_write_time(ec); !ec) se.mtime = toUnixSeconds(t);
        ec.clear();
        if (auto sz = entry.file_size(ec); !ec) se.size = static_cast<std::int64_t>(sz);
        ec.clear();
        entries.push_back(std::move(se));
    }

    tick(); // final tick — return value irrelevant, walk is done
    return entries;
}

LibraryScanner::Result
LibraryScanner::ingest(std::vector<ScanEntry> const & entries,
                       IngestProgressFn const & onProgress,
                       StopFn const & shouldStop)
{
    Result result;

    auto report = [&](int percent)
    {
        if (onProgress) onProgress(std::clamp(percent, 0, 100));
    };

    report(0);

    auto known = _repo.mtimes();
    std::unordered_set<std::string> seen;
    seen.reserve(entries.size());

    int const total = static_cast<int>(entries.size());
    int lastPercent = -1;

    for (int i = 0; i < total; ++i)
    {
        // Bail out promptly on teardown. The repository keeps whatever was
        // written so far; the deletion sweep is skipped so unseen rows are
        // not wrongly purged — the next scan reconciles incrementally.
        if (shouldStop && shouldStop())
            return result;

        ScanEntry const & e = entries[i];
        std::string const lext = lowerExt(e.path);

        if (lext == "pls")
        {
            // A `.pls` expands into N rows whose DB keys are synthetic
            // (`<pls>#CH<N>`) — the source path is never itself a key.
            // Always re-parse: the fast-path keys on e.path which the repo
            // never holds, and PLS files are small and rare.
            auto loaded = PlsReader::read(e.path);
            std::string const prefix = e.path.string() + "#";

            if (!loaded)
            {
                // Parsing failed (file unreadable, not "empty playlist"):
                // preserve any pre-existing rows rather than wiping them
                // on a transient I/O hiccup. Mark them seen so the sweep
                // below leaves them alone.
                for (auto const & [k, _] : known)
                    if (k.compare(0, prefix.size(), prefix) == 0)
                        seen.insert(k);
                continue;
            }

            // Per-file wipe: erase every existing channel row for this
            // PLS before re-emitting the current channel list. Channels
            // that disappeared or got renumbered are then naturally
            // counted by the deletion sweep at the bottom (since we do
            // NOT add the wiped keys to `seen`); only the keys we
            // actually re-upsert survive the sweep.
            for (auto const & [k, _] : known)
                if (k.compare(0, prefix.size(), prefix) == 0)
                    _repo.erase(k);

            for (auto & info : *loaded)
            {
                // Local-file channels inside a library-root .pls would
                // collide with the regular file scan's row for the same
                // path. The library only takes URL channels here — local
                // file playlists are M3U's job (no library indexing).
                if (!info.isStream()) continue;
                info.mtime = e.mtime;
                info.size  = e.size;
                std::string const childKey = info.path.string();
                bool const childPresent =
                    (known.find(childKey) != known.end());
                _repo.upsert(info);
                seen.insert(childKey);
                if (childPresent) ++result.updated;
                else              ++result.added;
            }
        }
        else
        {
            std::string const key = e.path.string();
            seen.insert(key);

            auto known_it = known.find(key);
            bool const present = (known_it != known.end());
            if (present && e.mtime != 0 && known_it->second == e.mtime)
            {
                ++result.skipped;
            }
            else
            {
                TrackInfo info = extractMetadata(e.path);
                info.mtime = e.mtime;
                info.size  = e.size;
                _repo.upsert(info);
                if (present) ++result.updated;
                else         ++result.added;
            }
        }

        // Report only when the integer percentage actually advances.
        int const percent = total > 0 ? (i + 1) * 100 / total : 100;
        if (percent != lastPercent)
        {
            lastPercent = percent;
            report(percent);
        }
    }

    // Sweep deletions: anything in the repo that we didn't see on disk.
    for (auto const & [key, _] : known)
    {
        if (seen.find(key) != seen.end()) continue;
        _repo.erase(key);
        ++result.removed;
    }

    report(100);
    return result;
}

} // namespace vtplayer
