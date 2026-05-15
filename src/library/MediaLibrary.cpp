// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "MediaLibrary.h"

namespace vtplayer
{

TrackInfo const * MediaLibrary::find(std::filesystem::path const & path) const
{
    auto it = _byPath.find(path.string());
    if (it == _byPath.end()) return nullptr;
    return &_tracks[it->second];
}

void MediaLibrary::clear()
{
    _tracks.clear();
    _byPath.clear();
}

void MediaLibrary::upsert(TrackInfo track)
{
    auto const key = track.path.string();
    auto it = _byPath.find(key);
    if (it != _byPath.end())
    {
        _tracks[it->second] = std::move(track);
        return;
    }
    _byPath.emplace(key, _tracks.size());
    _tracks.push_back(std::move(track));
}

void MediaLibrary::erase(std::filesystem::path const & path)
{
    auto it = _byPath.find(path.string());
    if (it == _byPath.end()) return;

    std::size_t const idx = it->second;
    std::size_t const last = _tracks.size() - 1;

    if (idx != last)
    {
        // Move-replace with the last element to keep the array compact.
        _tracks[idx] = std::move(_tracks[last]);
        _byPath[_tracks[idx].path.string()] = idx;
    }
    _tracks.pop_back();
    _byPath.erase(it);
}

} // namespace vtplayer
