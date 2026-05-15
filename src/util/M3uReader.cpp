// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "M3uReader.h"

#include "UnicodeNormalize.h"

#include <fstream>
#include <string>
#include <system_error>

namespace vtplayer::M3uReader
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

// Parse "#EXTINF:<duration>,<metadata>" into (duration, artist, title).
// Metadata convention: "Artist - Title" when " - " is present, otherwise the
// whole string is the title.
struct ExtInf
{
    float duration = 0.0f;
    std::string artist;
    std::string title;
};

ExtInf parseExtInf(std::string const & line)
{
    ExtInf info;
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

std::optional<std::vector<TrackInfo>> read(std::filesystem::path const & path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        return std::nullopt;
    }

    std::vector<TrackInfo> tracks;
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
            // Other extension tags (#EXTM3U, #PLAYLIST:, ...) — skip silently.
            continue;
        }

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

        tracks.push_back(std::move(info));
        havePending = false;
        pending = {};
    }

    return tracks;
}

} // namespace vtplayer::M3uReader
