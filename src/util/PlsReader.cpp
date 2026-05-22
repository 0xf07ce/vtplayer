// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PlsReader.h"

#include "UnicodeNormalize.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <system_error>

namespace vtplayer::PlsReader
{

namespace
{

void trim(std::string & s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.erase(s.begin());
    while (!s.empty()
           && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'
               || s.back() == '\n'))
        s.pop_back();
}

std::string toLowerAscii(std::string s)
{
    for (auto & c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Split a lowercase PLS key like "file12" into ("file", 12). Returns false
// for keys that don't end in an integer.
bool splitIndexedKey(std::string const & lkey, std::string & base, int & idx)
{
    auto first_digit = lkey.size();
    for (std::size_t i = lkey.size(); i > 0; --i)
    {
        char c = lkey[i - 1];
        if (c < '0' || c > '9') break;
        first_digit = i - 1;
    }
    if (first_digit == lkey.size() || first_digit == 0) return false;
    base = lkey.substr(0, first_digit);
    try
    {
        idx = std::stoi(lkey.substr(first_digit));
    }
    catch (...)
    {
        return false;
    }
    return idx > 0;
}

// A value like "http://...", "https://...", "mms://...", "icyx://...".
bool looksLikeUrl(std::string const & v)
{
    auto colon = v.find("://");
    if (colon == std::string::npos || colon == 0) return false;
    for (std::size_t i = 0; i < colon; ++i)
    {
        char c = v[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                  || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
        if (!ok) return false;
    }
    return true;
}

struct Entry
{
    std::string file;
    std::string title;
    int         length = 0;
};

} // namespace

std::optional<std::vector<TrackInfo>> read(std::filesystem::path const & path)
{
    std::ifstream in(path);
    if (!in.is_open()) return std::nullopt;

    std::ostringstream ss;
    ss << in.rdbuf();
    std::istringstream stream(ss.str());

    std::map<int, Entry> entries; // ordered by channel index
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
                section = toLowerAscii(line.substr(1, end - 1));
            continue;
        }
        if (section != "playlist") continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key   = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        trim(key);
        trim(value);
        if (key.empty()) continue;

        std::string const lkey = toLowerAscii(key);
        std::string base;
        int         idx = 0;
        if (!splitIndexedKey(lkey, base, idx)) continue;

        auto & e = entries[idx];
        if (base == "file")        e.file  = value;
        else if (base == "title")  e.title = value;
        else if (base == "length")
        {
            try { e.length = std::stoi(value); }
            catch (...) { e.length = 0; }
        }
        // NumberOfEntries / Version / other keys are deliberately ignored —
        // we trust the actual FileN keys we saw rather than a possibly-stale
        // count.
    }

    std::vector<TrackInfo> out;
    out.reserve(entries.size());

    std::error_code ec;
    auto absPls = std::filesystem::weakly_canonical(path, ec);
    if (ec || absPls.empty()) absPls = std::filesystem::absolute(path, ec);
    if (ec) absPls = path;

    auto const album   = toNfc(absPls.stem().string());
    auto const baseDir = absPls.parent_path();

    for (auto const & [idx, e] : entries)
    {
        if (e.file.empty()) continue;

        TrackInfo info;
        info.album    = album;
        info.duration = (e.length > 0) ? static_cast<float>(e.length) : 0.0f;

        if (looksLikeUrl(e.file))
        {
            std::string synth = absPls.string();
            synth += "#CH";
            synth += std::to_string(idx);
            info.path      = synth;
            info.streamUrl = e.file;
            info.format    = AudioFormat::Stream;
        }
        else
        {
            std::filesystem::path trackPath(e.file);
            if (trackPath.is_relative() && !baseDir.empty())
                trackPath = baseDir / trackPath;
            std::error_code ec2;
            auto normalized = std::filesystem::weakly_canonical(trackPath, ec2);
            if (!ec2 && !normalized.empty())
                trackPath = std::move(normalized);
            else
                trackPath = trackPath.lexically_normal();
            info.path   = std::move(trackPath);
            info.format = TrackInfo::formatFromPath(info.path);
        }

        std::string title = e.title;
        if (title.empty())
        {
            if (info.isStream())
            {
                // Fall back to the URL host or the URL itself.
                auto const & u = info.streamUrl;
                auto scheme = u.find("://");
                if (scheme != std::string::npos)
                {
                    auto host_start = scheme + 3;
                    auto host_end   = u.find_first_of("/?#", host_start);
                    title = (host_end == std::string::npos)
                                ? u.substr(host_start)
                                : u.substr(host_start, host_end - host_start);
                }
                if (title.empty()) title = "Stream " + std::to_string(idx);
            }
            else
            {
                title = info.path.stem().string();
                if (title.empty()) title = "Track " + std::to_string(idx);
            }
        }
        info.title = toNfc(title);

        out.push_back(std::move(info));
    }

    return out;
}

} // namespace vtplayer::PlsReader
