// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PlaylistStore.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace vtplayer
{

namespace
{

constexpr char const * kExt = ".m3u8";

/// Turn an arbitrary user-typed name into a safe single-segment filename
/// stem. Trims surrounding whitespace, drops a user-typed trailing ".m3u8"
/// (case-insensitive), and replaces path separators / control characters
/// with '_'. Returns an empty string for names that reduce to nothing or to
/// the reserved "." / ".." entries.
std::string sanitizeName(std::string const & raw)
{
    // Trim leading/trailing whitespace.
    std::size_t b = 0;
    std::size_t e = raw.size();
    while (b < e && std::isspace(static_cast<unsigned char>(raw[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(raw[e - 1]))) --e;
    std::string name = raw.substr(b, e - b);

    // Strip a user-typed ".m3u8" extension so we never produce "foo.m3u8.m3u8".
    if (name.size() >= 5)
    {
        std::string tail = name.substr(name.size() - 5);
        std::transform(tail.begin(), tail.end(), tail.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (tail == kExt)
            name.erase(name.size() - 5);
    }

    // Replace separators / NUL / control chars.
    for (char & c : name)
    {
        unsigned char const u = static_cast<unsigned char>(c);
        if (c == '/' || c == '\\' || u < 0x20)
            c = '_';
    }

    // Re-trim in case the extension strip exposed trailing space.
    std::size_t tb = 0;
    std::size_t te = name.size();
    while (tb < te && std::isspace(static_cast<unsigned char>(name[tb]))) ++tb;
    while (te > tb && std::isspace(static_cast<unsigned char>(name[te - 1]))) --te;
    name = name.substr(tb, te - tb);

    if (name == "." || name == "..")
        return {};
    return name;
}

} // namespace

std::filesystem::path PlaylistStore::defaultDir()
{
    char const * home = std::getenv("HOME");
    if (!home) return {};
    return std::filesystem::path(home) / ".config" / "vtplayer" / "playlists";
}

PlaylistStore::PlaylistStore(std::filesystem::path dir) : _dir(std::move(dir)) {}

std::filesystem::path PlaylistStore::pathFor(std::string const & name) const
{
    return _dir / (sanitizeName(name) + kExt);
}

std::vector<std::string> PlaylistStore::list() const
{
    std::vector<std::string> names;
    if (_dir.empty()) return names;

    std::error_code ec;
    if (!std::filesystem::exists(_dir, ec)) return names;

    for (auto const & entry : std::filesystem::directory_iterator(_dir, ec))
    {
        if (ec) break;
        std::error_code fec;
        if (!entry.is_regular_file(fec)) continue;
        auto const & p = entry.path();
        if (p.extension() == kExt)
            names.push_back(p.stem().string());
    }

    std::sort(names.begin(), names.end(),
              [](std::string const & a, std::string const & b) {
                  return std::lexicographical_compare(
                      a.begin(), a.end(), b.begin(), b.end(),
                      [](unsigned char x, unsigned char y) {
                          return std::tolower(x) < std::tolower(y);
                      });
              });
    return names;
}

std::optional<std::string> PlaylistStore::create(std::string const & name)
{
    if (_dir.empty()) return std::nullopt;
    std::string const clean = sanitizeName(name);
    if (clean.empty()) return std::nullopt;

    std::filesystem::path const file = _dir / (clean + kExt);

    std::error_code ec;
    if (std::filesystem::exists(file, ec)) return std::nullopt; // reject collision

    std::filesystem::create_directories(_dir, ec);
    if (ec) return std::nullopt;

    // Write a valid Extended-M3U header so a later track-append step and the
    // existing M3uReader both handle the empty playlist cleanly.
    std::ofstream out(file, std::ios::trunc);
    if (!out) return std::nullopt;
    out << "#EXTM3U\n";
    if (!out) return std::nullopt;

    return clean;
}

std::optional<std::string> PlaylistStore::rename(std::string const & oldName,
                                                 std::string const & newName)
{
    if (_dir.empty()) return std::nullopt;
    std::string const from = sanitizeName(oldName);
    std::string const to = sanitizeName(newName);
    if (from.empty() || to.empty()) return std::nullopt;
    if (from == to) return to; // unchanged name — nothing to do, report success

    std::filesystem::path const src = _dir / (from + kExt);
    std::filesystem::path const dst = _dir / (to + kExt);

    std::error_code ec;
    if (!std::filesystem::exists(src, ec)) return std::nullopt; // source gone
    if (std::filesystem::exists(dst, ec)) return std::nullopt;  // reject collision

    std::filesystem::rename(src, dst, ec);
    if (ec) return std::nullopt;
    return to;
}

bool PlaylistStore::remove(std::string const & name)
{
    if (_dir.empty()) return false;
    std::string const clean = sanitizeName(name);
    if (clean.empty()) return false;

    std::error_code ec;
    return std::filesystem::remove(_dir / (clean + kExt), ec);
}

bool PlaylistStore::append(std::string const & name, TrackInfo const & track) const
{
    if (_dir.empty()) return false;
    std::string const clean = sanitizeName(name);
    if (clean.empty()) return false;

    std::filesystem::path const file = _dir / (clean + kExt);

    std::error_code ec;
    bool const existed = std::filesystem::exists(file, ec);

    // Ensure the directory exists so a never-created store still works.
    std::filesystem::create_directories(_dir, ec);

    std::ofstream out(file, std::ios::app);
    if (!out) return false;

    // create() normally seeds the header, but append tolerates a missing file
    // (hand-deleted between sessions) by re-seeding it here.
    if (!existed)
        out << "#EXTM3U\n";

    int const dur = (track.duration > 0.0f) ? static_cast<int>(track.duration) : 0;
    std::string meta = track.artist.empty() ? track.title
                                             : track.artist + " - " + track.title;
    out << "#EXTINF:" << dur << ',' << meta << '\n';

    std::string const location =
        track.isStream() ? track.streamUrl : track.path.string();
    out << location << '\n';

    return static_cast<bool>(out);
}

} // namespace vtplayer
