// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Theme.h"
#include "../playqueue/PlayQueue.h"

#include <ventty/widget/Widget.h>

#include <filesystem>
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

    /// Title shown in the header (defaults to "Play Queue"). Application points
    /// it at the active playlist's name while a playlist fills the queue.
    void setTitle(std::string title) { _title = std::move(title); }

    void addTrack(TrackInfo const & track);

    /// Insert a track at `idx` (clamped to [0, tracks.size()]). Shifts the
    /// playing/selected indices forward for entries at or after `idx`.
    void insertTrack(int idx, TrackInfo const & track);

    void removeSelected();
    void clear();

    /// Replace the full track list (used when switching play queues). Resets scroll and selection.
    void setTracks(std::vector<TrackInfo> tracks);

    /// Snapshot of the current tracks for persistence.
    std::vector<TrackInfo> const & tracks() const { return _queue.tracks(); }

    int selectedIndex() const { return _selectedIndex; }
    void setSelectedIndex(int idx);
    int playingIndex() const { return _playingIndex; }
    void setPlayingIndex(int idx) { _playingIndex = idx; }

    /// Scroll the list so the currently-playing track sits at the top of the
    /// visible area, then move the cursor onto it. No-op if nothing is
    /// playing.
    void focusPlayingTrack();

    TrackInfo const * selectedTrack() const;
    /// The multi-selection unioned with the cursor row, in ascending index
    /// order. Used by the "add to playlist" action.
    std::vector<TrackInfo> selectedTracks() const;
    TrackInfo const * track(int idx) const;
    TrackInfo const * findTrack(std::filesystem::path const & path) const;
    bool updateTrackInfo(std::filesystem::path const & path,
                         TrackInfo const & replacement);
    bool replaceTrackPath(std::filesystem::path const & oldPath,
                          TrackInfo const & replacement);
    int trackCount() const { return _queue.size(); }
    bool empty() const { return _queue.empty(); }

    using OnPlayCallback = std::function<void(int index)>;
    void setOnPlay(OnPlayCallback cb) { _onPlay = std::move(cb); }

    /// Fired when a track that was playing got removed (or the play queue was
    /// replaced/cleared while a track was playing). Application uses this to
    /// stop audio playback.
    using OnPlayingRemovedCallback = std::function<void()>;
    void setOnPlayingRemoved(OnPlayingRemovedCallback cb) { _onPlayingRemoved = std::move(cb); }

    /// Fired whenever the queue's *contents* change by any path — add, insert,
    /// remove, reorder, clear, or full replace. Application uses it to reset the
    /// header title back to the default (the loaded playlist's name is only
    /// re-stamped by the playlist-load path). Distinct from OnPlayingRemoved,
    /// which fires only when a *playing* track is lost.
    using OnContentsChangedCallback = std::function<void()>;
    void setOnContentsChanged(OnContentsChangedCallback cb) { _onContentsChanged = std::move(cb); }

    /// Multi-selection state (visual prep — bulk actions are a future feature).
    bool isMultiSelected(int idx) const { return _multiSelected.count(idx) > 0; }
    void clearMultiSelection();
    void selectAll();

    /// Move the whole contiguous selection (multi-select ∪ cursor) one row up /
    /// down, carrying the playing-track highlight with it.
    void moveSelectionUp();
    void moveSelectionDown();

    void draw(ventty::Window & window) override;
    bool handleKey(ventty::KeyEvent const & event) override;
    bool handleMouse(ventty::MouseEvent const & event);

protected:
    void onFocusChanged() override;

private:
    void scrollToSelected();
    void extendSelectionTo(int newIndex);
    void shiftSelection(int delta);
    void notifyContentsChanged() { if (_onContentsChanged) _onContentsChanged(); }

    Theme _theme;
    std::string _title = "Play Queue";
    PlayQueue _queue;
    int _selectedIndex = 0;
    int _scrollOffset = 0;
    int _playingIndex = -1;
    std::set<int> _multiSelected;
    int _selectionAnchor = -1;
    OnPlayCallback _onPlay;
    OnPlayingRemovedCallback _onPlayingRemoved;
    OnContentsChangedCallback _onContentsChanged;
};

} // namespace vtplayer
