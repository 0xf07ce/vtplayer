// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "StreamList.h"

#include "UnicodeNormalize.h"

#include <cstdlib>
#include <fstream>
#include <system_error>

namespace vtplayer::StreamList
{

namespace
{

void trim(std::string & s)
{
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' ||
                          s.back() == ' '  || s.back() == '\t'))
        s.pop_back();
    auto first = s.find_first_not_of(" \t");
    if (first == std::string::npos)
        s.clear();
    else if (first > 0)
        s.erase(0, first);
}

// A reasonable display name when the file has no #EXTINF for a URL.
std::string nameFromUrl(std::string const & url)
{
    auto scheme = url.find("://");
    std::size_t start = (scheme == std::string::npos) ? 0 : scheme + 3;
    auto slash = url.find('/', start);
    return url.substr(start, slash == std::string::npos ? std::string::npos
                                                        : slash - start);
}

constexpr char const * kDefaultContent =
    "#EXTM3U\n"
    "# vtplayer radio streams. One stream per pair of lines:\n"
    "#   #EXTINF:-1,<display name>\n"
    "#   <stream URL>\n"
    "# Lines starting with # (other than #EXTINF) are ignored.\n"
    "\n"
    "# NOTE: the MBC URL below carries a session token (?_lsu_sa_=...) that\n"
    "# expires. When it stops working, copy a fresh URL from the MBC mini\n"
    "# player and replace the line.\n"
    "#EXTINF:-1,MBC FM4U (mini)\n"
    "https://minimw.imbc.com/dmfm/_definst_/mfm.stream/playlist.m3u8?_lsu_sa_=6C217E17132E3E34BD4F355E34C1644CF5FD3065B509E214328031a1F67538E6E7a203F837D2F147B0E133910CbDC1FC7D14D88CE662FBA6172550F5D0C011E7333EFE9DA5D8E31F280FC76F358515A2E9894F73A946EF40EEA4469FDE3D5638D5C456F0B600152329692CEA087EC6EA\n";

} // namespace

std::filesystem::path defaultPath()
{
    char const * home = std::getenv("HOME");
    if (!home)
        return {};
    return std::filesystem::path(home) / ".config" / "vtplayer" / "streams.m3u";
}

std::vector<Stream> load()
{
    auto const path = defaultPath();
    if (path.empty())
        return {};

    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
    {
        // Seed an editable example on first use.
        if (auto parent = path.parent_path(); !parent.empty())
            std::filesystem::create_directories(parent, ec);
        std::ofstream out(path);
        if (out)
            out << kDefaultContent;
    }

    std::ifstream file(path);
    if (!file.is_open())
        return {};

    std::vector<Stream> streams;
    std::string line;
    std::string pendingName;
    bool havePending = false;

    while (std::getline(file, line))
    {
        trim(line);
        if (line.empty())
            continue;

        if (line.rfind("#EXTINF:", 0) == 0)
        {
            auto comma = line.find(',');
            pendingName = (comma == std::string::npos)
                              ? std::string()
                              : line.substr(comma + 1);
            trim(pendingName);
            havePending = true;
            continue;
        }
        if (line[0] == '#')
            continue; // #EXTM3U, comments, other tags

        Stream s;
        s.url  = line;
        s.name = toNfc(havePending && !pendingName.empty()
                           ? pendingName
                           : nameFromUrl(line));
        streams.push_back(std::move(s));
        havePending = false;
        pendingName.clear();
    }

    return streams;
}

} // namespace vtplayer::StreamList
