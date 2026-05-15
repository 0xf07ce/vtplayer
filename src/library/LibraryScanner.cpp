// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "LibraryScanner.h"

#include "LibraryRepository.h"
#include "MediaLibrary.h"
#include "../util/UnicodeNormalize.h"

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

LibraryScanner::LibraryScanner(MediaLibrary & library, LibraryRepository & repo)
    : _library(library), _repo(repo)
{
}

LibraryScanner::Result LibraryScanner::scan(fs::path const & root,
                                            std::vector<std::string> const & extensions)
{
    Result result;
    if (root.empty()) return result;

    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) return result;

    std::unordered_set<std::string> extSet;
    for (auto e : extensions)
    {
        for (auto & c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!e.empty() && e[0] == '.') e.erase(0, 1);
        if (!e.empty()) extSet.insert(std::move(e));
    }

    auto known = _repo.mtimes();
    std::unordered_set<std::string> seen;
    seen.reserve(known.size());

    fs::recursive_directory_iterator it(
        root, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec))
    {
        if (ec) { ec.clear(); continue; }
        auto const & entry = *it;

        if (!entry.is_regular_file(ec)) { ec.clear(); continue; }
        auto const & p = entry.path();

        std::string const ext = lowerExt(p);
        if (extSet.find(ext) == extSet.end()) continue;

        std::string const key = p.string();
        seen.insert(key);

        std::int64_t mtime = 0;
        std::int64_t size  = 0;
        if (auto t = entry.last_write_time(ec); !ec) mtime = toUnixSeconds(t);
        ec.clear();
        if (auto sz = entry.file_size(ec); !ec) size = static_cast<std::int64_t>(sz);
        ec.clear();

        auto known_it = known.find(key);
        bool const present = (known_it != known.end());
        if (present && mtime != 0 && known_it->second == mtime)
        {
            ++result.skipped;
            continue;
        }

        TrackInfo info = extractMetadata(p);
        info.mtime = mtime;
        info.size  = size;
        _library.upsert(info);
        _repo.upsert(info);

        if (present) ++result.updated;
        else         ++result.added;
    }

    // Sweep deletions: anything in the repo that we didn't see on disk.
    for (auto const & [key, _] : known)
    {
        if (seen.find(key) != seen.end()) continue;
        _library.erase(key);
        _repo.erase(key);
        ++result.removed;
    }

    return result;
}

} // namespace vtplayer
