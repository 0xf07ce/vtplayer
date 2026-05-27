// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "../audio/TrackInfo.h"

#include <vector>

namespace vtplayer
{

/// Volatile in-memory list of tracks queued for playback. The PlayQueue owns
/// only the track data; UI concerns (selection, scroll, currently-playing
/// index) live in PlayQueueView. Persistence is handled separately by
/// PlayQueueCache (path-list snapshot resolved against the MediaLibrary).
class PlayQueue
{
public:
    std::vector<TrackInfo> const & tracks() const { return _tracks; }
    int  size()  const { return static_cast<int>(_tracks.size()); }
    bool empty() const { return _tracks.empty(); }

    TrackInfo const * at(int idx) const;

    void setTracks(std::vector<TrackInfo> tracks) { _tracks = std::move(tracks); }
    void clear() { _tracks.clear(); }

    void addTrack(TrackInfo track) { _tracks.push_back(std::move(track)); }

    /// `idx` is clamped to [0, size()].
    void insertTrack(int idx, TrackInfo track);

    /// No-op for out-of-range indices.
    void removeAt(int idx);

    /// Swap two entries. No-op if either index is out of range or equal.
    void swap(int i, int j);

private:
    std::vector<TrackInfo> _tracks;
};

} // namespace vtplayer
