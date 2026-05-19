// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace vtplayer
{

/// A single internet-radio entry from `streams.m3u`.
struct Stream
{
    std::string name;
    std::string url;
};

namespace StreamList
{

/// Canonical path: `~/.config/vtplayer/streams.m3u`.
std::filesystem::path defaultPath();

/// Read the stream list. Streams use Extended-M3U form (`#EXTINF:-1,Name`
/// then a URL line); the URL is kept verbatim (never path-normalized). If
/// the file does not exist it is created with a commented example so the
/// user has something to edit. Returns the parsed entries (possibly empty).
std::vector<Stream> load();

} // namespace StreamList

} // namespace vtplayer
