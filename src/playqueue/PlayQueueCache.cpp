// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PlayQueueCache.h"

#include "../library/MediaLibrary.h"
#include "../util/UnicodeNormalize.h"

#include <cstdlib>
#include <fstream>
#include <system_error>

namespace vtplayer::PlayQueueCache
{

std::filesystem::path defaultPath()
{
    char const * home = std::getenv("HOME");
    if (!home) return {};
    return std::filesystem::path(home) / ".config" / "vtplayer" / "playqueue.cache";
}

bool save(std::vector<TrackInfo> const & tracks)
{
    auto path = defaultPath();
    if (path.empty()) return false;

    std::error_code ec;
    if (auto parent = path.parent_path(); !parent.empty())
    {
        std::filesystem::create_directories(parent, ec);
        if (ec) return false;
    }

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) return false;

    file << "# vtplayer play queue session — list of absolute paths\n";
    for (auto const & t : tracks)
    {
        file << t.path.string() << '\n';
    }
    return static_cast<bool>(file);
}

std::vector<TrackInfo> restore(MediaLibrary const & library)
{
    std::vector<TrackInfo> result;
    auto path = defaultPath();
    if (path.empty()) return result;

    std::ifstream file(path);
    if (!file.is_open()) return result;

    std::string line;
    while (std::getline(file, line))
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
        {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') continue;

        std::filesystem::path trackPath(line);
        if (auto * indexed = library.find(trackPath))
        {
            result.push_back(*indexed);
            continue;
        }

        // Path not in the library index — fall back to a minimal record so
        // external imports survive across runs.
        TrackInfo info;
        info.path   = std::move(trackPath);
        info.format = TrackInfo::formatFromPath(info.path);
        info.title  = toNfc(info.path.stem().string());
        result.push_back(std::move(info));
    }
    return result;
}

} // namespace vtplayer::PlayQueueCache
