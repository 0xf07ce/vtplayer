// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "../audio/TrackInfo.h"
#include "Theme.h"

#include <ventty/widget/Widget.h>

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace vtplayer
{

/// Left-panel browser for saved playlists (mode 5). It has two views:
///   - List view: the saved playlists by name (the host feeds names via
///     setItems() after querying PlaylistStore).
///   - Contents view: the tracks of one opened playlist, with a ".." row on
///     top to return to the list — mirroring FileBrowser's folder drill-in.
/// It holds no disk logic: Enter on a playlist row fires OnOpen, and the host
/// reads the file and feeds the tracks back via showContents(). Selection /
/// scroll for both views live here. Creating, renaming and deleting playlists
/// is driven from the host's ESC menu, not from this widget.
class PlaylistsView : public ventty::Widget
{
public:
    /// Fired when the user opens a playlist row (Enter in list view). The host
    /// reads the playlist and calls showContents() with the resolved tracks.
    using OnOpen = std::function<void(std::string const & name)>;
    /// Fired when the user activates the contents view (Enter): replace the
    /// play queue with the current selection (multi-selection unioned with the
    /// cursor, in row order) and play, matching the Enter semantics of
    /// LibraryView / FileBrowser. A bare cursor yields a single-track list.
    using OnPlayTracks = std::function<void(std::vector<TrackInfo> const & tracks)>;
    /// Fired on Ctrl+S in the contents view's edit mode: persist the current
    /// (possibly reordered / trimmed) track list. The host writes the file and
    /// returns true on success, which leaves edit mode.
    using OnSaveTracks =
        std::function<bool(std::string const & name, std::vector<TrackInfo> const & tracks)>;

    void setTheme(Theme const & theme) { _theme = theme; }
    void setOnOpen(OnOpen cb) { _onOpen = std::move(cb); }
    void setOnPlayTracks(OnPlayTracks cb) { _onPlayTracks = std::move(cb); }
    void setOnSaveTracks(OnSaveTracks cb) { _onSaveTracks = std::move(cb); }

    /// Replace the displayed names (caller supplies them already sorted).
    /// Clamps selection / scroll so deleting the last row stays valid.
    void setItems(std::vector<std::string> names);

    /// Enter the contents view for `name`, showing `tracks` under a ".." row.
    /// Called by the host after it reads the playlist file.
    void showContents(std::string name, std::vector<TrackInfo> tracks);
    /// Replace the open playlist's tracks in place, keeping the contents view,
    /// the open name, edit mode and the cursor/scroll position (clamped to the
    /// new size). Used to reflect an external append to the currently-open
    /// playlist without the full reset showContents() does. The transient
    /// multi-selection is dropped since row indices may have shifted.
    void reloadContents(std::vector<TrackInfo> tracks);
    /// Leave the contents view, returning to the playlist list.
    void closeContents();
    /// True while one playlist's tracks are shown.
    bool inContents() const { return _inContents; }

    /// Contents-view edit-mode control, mirrored by the host's ESC menu toggle
    /// ("Edit playlist" / "Save playlist") and the Ctrl+E / Ctrl+S keys.
    bool inEditMode() const { return _inContents && _editMode; }
    /// Arm editing (no-op outside the contents view).
    void enterEditMode() { if (_inContents) _editMode = true; }
    /// Persist the (possibly reordered / trimmed) tracks via OnSaveTracks and
    /// leave edit mode on success. Returns whether the save succeeded; a failed
    /// write keeps edit mode on so the user can retry.
    bool saveEdits();

    /// Name under the cursor in list view, or the open playlist's name in
    /// contents view. "" when the list is empty.
    std::string selectedName() const;
    /// Track under the cursor in contents view, or nullptr (when on the ".."
    /// row, or in list view). Used by the host's `a`-append handler.
    TrackInfo const * selectedTrack() const;
    /// The multi-selection unioned with the cursor track (in row order),
    /// excluding the ".." row. Empty in list view or on a bare ".." cursor.
    /// Used by the host to push a whole selection to the play queue.
    std::vector<TrackInfo> selectedTracks() const;
    bool empty() const { return _names.empty(); }

    void draw(ventty::Window & window) override;
    bool handleKey(ventty::KeyEvent const & event) override;
    bool handleMouse(ventty::MouseEvent const & event);

protected:
    /// Clear the track multi-selection when focus leaves the panel, matching
    /// FileBrowser / PlayQueueView so a stale selection can't linger off-screen.
    void onFocusChanged() override;

private:
    void moveCursor(int delta);
    void ensureVisible(int listH);
    void drawList(ventty::Window & window);
    void drawContents(ventty::Window & window);

    // Contents-view editing (gated behind Ctrl+E edit mode). Selection lives in
    // row-index space (row 0 = ".."), which is never selectable — mirrors
    // FileBrowser's parent-row exclusion.
    void extendTrackSelectionTo(int newRow);
    void selectAllTracks();
    void clearTrackSelection();
    void removeSelectedTracks();
    void moveTrackSelectionUp();
    void moveTrackSelectionDown();

    Theme _theme;
    std::vector<std::string> _names;
    int _selectedIndex = 0;
    int _scrollOffset = 0;

    // Contents view: the tracks of one opened playlist. Row 0 is the ".."
    // back-row; tracks occupy rows 1..N, so _trackSel == 0 means "..".
    bool _inContents = false;
    std::string _openName;
    std::vector<TrackInfo> _tracks;
    int _trackSel = 0;
    int _trackScroll = 0;
    bool _editMode = false;          // toggled by Ctrl+E; gates all edit ops
    std::set<int> _trackMultiSel;    // row indices (never includes row 0)
    int _trackAnchor = -1;           // shift-range anchor; -1 when none

    OnOpen _onOpen;
    OnPlayTracks _onPlayTracks;
    OnSaveTracks _onSaveTracks;
};

} // namespace vtplayer
