// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PlayQueue.h"

#include "../util/UnicodeNormalize.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <system_error>

namespace vtplayer
{

namespace
{

// Strip UTF-8 BOM and trailing CR from a raw line.
void normalizeLine(std::string & line, bool stripBom)
{
    if (stripBom && line.size() >= 3 &&
        static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB &&
        static_cast<unsigned char>(line[2]) == 0xBF)
    {
        line.erase(0, 3);
    }
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
    {
        line.pop_back();
    }
    auto first = line.find_first_not_of(" \t");
    if (first == std::string::npos)
    {
        line.clear();
    }
    else if (first > 0)
    {
        line.erase(0, first);
    }
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
    {
        line.pop_back();
    }
}

// Parse an "#EXTINF:<duration>,<metadata>" line into (duration_seconds, artist, title).
// Metadata convention: "Artist - Title" when a " - " delimiter is present, otherwise the
// entire string is treated as the title.
struct ExtInf
{
    float duration = 0.0f;
    std::string artist;
    std::string title;
};

ExtInf parseExtInf(std::string const & line)
{
    ExtInf info;
    // line starts with "#EXTINF:"
    auto comma = line.find(',', 8);
    std::string durStr = (comma == std::string::npos) ? line.substr(8) : line.substr(8, comma - 8);
    std::string meta   = (comma == std::string::npos) ? std::string() : line.substr(comma + 1);

    try
    {
        int d = std::stoi(durStr);
        info.duration = (d > 0) ? static_cast<float>(d) : 0.0f;
    }
    catch (...) {}

    auto sep = meta.find(" - ");
    if (sep != std::string::npos)
    {
        info.artist = meta.substr(0, sep);
        info.title  = meta.substr(sep + 3);
    }
    else
    {
        info.title = std::move(meta);
    }
    return info;
}

} // namespace

std::string PlayQueue::name() const
{
    return toNfc(_path.stem().string());
}

std::optional<PlayQueue> PlayQueue::load(std::filesystem::path const & path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        return std::nullopt;
    }

    PlayQueue pl(path);
    auto const baseDir = path.parent_path();

    std::string line;
    bool firstLine = true;
    ExtInf pending;
    bool havePending = false;

    while (std::getline(file, line))
    {
        normalizeLine(line, firstLine);
        firstLine = false;

        if (line.empty()) continue;

        if (line.rfind("#EXTINF:", 0) == 0)
        {
            pending = parseExtInf(line);
            havePending = true;
            continue;
        }
        if (line[0] == '#')
        {
            // Any other extension tag (e.g. #EXTM3U, #PLAYLIST:) — skip silently.
            continue;
        }

        // Plain path line.
        std::filesystem::path trackPath(line);
        if (trackPath.is_relative() && !baseDir.empty())
        {
            trackPath = baseDir / trackPath;
        }
        std::error_code ec;
        auto normalized = std::filesystem::weakly_canonical(trackPath, ec);
        if (!ec && !normalized.empty())
        {
            trackPath = normalized;
        }
        else
        {
            trackPath = trackPath.lexically_normal();
        }

        TrackInfo info;
        info.path     = std::move(trackPath);
        info.format   = TrackInfo::formatFromPath(info.path);
        info.duration = havePending ? pending.duration : 0.0f;
        info.artist   = havePending ? std::move(pending.artist) : std::string();
        info.title    = havePending ? std::move(pending.title)  : std::string();
        if (info.title.empty())
        {
            info.title = info.path.stem().string();
        }
        info.artist = toNfc(info.artist);
        info.title  = toNfc(info.title);

        pl._tracks.push_back(std::move(info));
        havePending = false;
        pending = {};
    }

    return pl;
}

bool PlayQueue::save() const
{
    if (_path.empty()) return false;

    std::error_code ec;
    if (auto parent = _path.parent_path(); !parent.empty())
    {
        std::filesystem::create_directories(parent, ec);
        if (ec) return false;
    }

    std::ofstream file(_path, std::ios::trunc);
    if (!file.is_open()) return false;

    file << "#EXTM3U\n";
    for (auto const & t : _tracks)
    {
        int const dur = (t.duration > 0.0f) ? static_cast<int>(std::lround(t.duration)) : -1;
        file << "#EXTINF:" << dur << ',';
        if (!t.artist.empty() && !t.title.empty())
        {
            file << t.artist << " - " << t.title;
        }
        else if (!t.title.empty())
        {
            file << t.title;
        }
        else
        {
            file << t.path.stem().string();
        }
        file << '\n';
        file << t.path.string() << '\n';
    }

    return static_cast<bool>(file);
}

} // namespace vtplayer
