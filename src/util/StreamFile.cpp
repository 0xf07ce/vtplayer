// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "StreamFile.h"

#include <fstream>
#include <sstream>

namespace vtplayer::StreamFile
{

namespace
{

void trim(std::string & s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.erase(s.begin());
    while (!s.empty()
           && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.pop_back();
}

} // namespace

std::optional<Meta> load(std::filesystem::path const & path)
{
    std::ifstream file(path);
    if (!file.is_open())
        return std::nullopt;

    std::ostringstream ss;
    ss << file.rdbuf();
    std::istringstream stream(ss.str());

    Meta out;
    std::string section;
    std::string line;
    while (std::getline(stream, line))
    {
        trim(line);
        if (line.empty()) continue;
        if (line.front() == '#' || line.front() == ';') continue;

        if (line.front() == '[')
        {
            auto end = line.find(']');
            if (end != std::string::npos)
                section = line.substr(1, end - 1);
            continue;
        }

        if (section != "stream") continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key   = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        trim(key);
        trim(value);

        if      (key == "url")   out.url   = value;
        else if (key == "title") out.title = value;
        else if (key == "album") out.album = value;
        else if (key == "genre") out.genre = value;
        else if (key == "year")
        {
            try { out.year = std::stoi(value); }
            catch (...) { out.year = 0; }
        }
        // Other keys (including artist / album_artist) are intentionally
        // ignored: stream tracks live under the (stream) artist node.
    }

    if (out.url.empty())
        return std::nullopt;
    return out;
}

} // namespace vtplayer::StreamFile
