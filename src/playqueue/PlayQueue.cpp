// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PlayQueue.h"

#include <algorithm>

namespace vtplayer
{

TrackInfo const * PlayQueue::at(int idx) const
{
    if (idx < 0 || idx >= static_cast<int>(_tracks.size())) return nullptr;
    return &_tracks[idx];
}

void PlayQueue::insertTrack(int idx, TrackInfo track)
{
    if (idx < 0) idx = 0;
    int const sz = static_cast<int>(_tracks.size());
    if (idx > sz) idx = sz;
    _tracks.insert(_tracks.begin() + idx, std::move(track));
}

void PlayQueue::removeAt(int idx)
{
    if (idx < 0 || idx >= static_cast<int>(_tracks.size())) return;
    _tracks.erase(_tracks.begin() + idx);
}

void PlayQueue::swap(int i, int j)
{
    int const sz = static_cast<int>(_tracks.size());
    if (i == j || i < 0 || j < 0 || i >= sz || j >= sz) return;
    std::swap(_tracks[i], _tracks[j]);
}

} // namespace vtplayer
