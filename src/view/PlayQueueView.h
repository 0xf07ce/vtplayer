// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Theme.h"
#include "../audio/TrackInfo.h"

#include <ventty/widget/Widget.h>

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace vtplayer
{

class PlayQueueView : public ventty::Widget
{
public:
    void setTheme(Theme const & theme) { _theme = theme; }

    void addTrack(TrackInfo const & track);

    /// Insert a track at `idx` (clamped to [0, tracks.size()]). Shifts the
    /// playing/selected indices forward for entries at or after `idx`.
    void insertTrack(int idx, TrackInfo const & track);

    void removeSelected();
    void moveSelectedUp();
    void moveSelectedDown();
    void clear();

    /// Randomize the order of tracks in place. The currently-playing track
    /// keeps its identity — its index is updated to its new position so audio
    /// playback stays uninterrupted.
    void shuffle();

    /// Replace the full track list (used when switching play queues). Resets scroll and selection.
    void setTracks(std::vector<TrackInfo> tracks);

    /// Snapshot of the current tracks for persistence.
    std::vector<TrackInfo> const & tracks() const { return _tracks; }

    int selectedIndex() const { return _selectedIndex; }
    void setSelectedIndex(int idx);
    int playingIndex() const { return _playingIndex; }
    void setPlayingIndex(int idx) { _playingIndex = idx; }

    TrackInfo const * selectedTrack() const;
    TrackInfo const * track(int idx) const;
    int trackCount() const { return static_cast<int>(_tracks.size()); }
    bool empty() const { return _tracks.empty(); }

    using OnPlayCallback = std::function<void(int index)>;
    void setOnPlay(OnPlayCallback cb) { _onPlay = std::move(cb); }

    /// Fired when a track that was playing got removed (or the play queue was
    /// replaced/cleared while a track was playing). Application uses this to
    /// stop audio playback.
    using OnPlayingRemovedCallback = std::function<void()>;
    void setOnPlayingRemoved(OnPlayingRemovedCallback cb) { _onPlayingRemoved = std::move(cb); }

    /// Multi-selection state (visual prep — bulk actions are a future feature).
    bool isMultiSelected(int idx) const { return _multiSelected.count(idx) > 0; }
    void clearMultiSelection();
    void selectAll();

    void draw(ventty::Window & window) override;
    bool handleKey(ventty::KeyEvent const & event) override;
    bool handleMouse(ventty::MouseEvent const & event);

protected:
    void onFocusChanged() override;

private:
    void scrollToSelected();
    void extendSelectionTo(int newIndex);

    Theme _theme;
    std::vector<TrackInfo> _tracks;
    int _selectedIndex = 0;
    int _scrollOffset = 0;
    int _playingIndex = -1;
    std::set<int> _multiSelected;
    int _selectionAnchor = -1;
    OnPlayCallback _onPlay;
    OnPlayingRemovedCallback _onPlayingRemoved;
};

} // namespace vtplayer
