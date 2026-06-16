// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Theme.h"
#include "../audio/TrackInfo.h"

#include <ventty/widget/Widget.h>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace vtplayer
{

class MediaLibrary;

/// Hierarchical browser over the MediaLibrary index. Two modes:
///   - AlbumArtistTree: Grouping > AlbumArtist fallback Artist > Album > Track
///   - Directory:       folder tree under the library root (from the DB index)
/// The Album projection uses `TrackInfo::grouping` as depth 0 (ID3v2 TIT1 /
/// Vorbis GROUPING / MP4 ©grp), then album-artist with artist fallback.
/// Both modes start fully collapsed; locate() unfolds only ancestors of the
/// focused track. Selection actions go out via callbacks so the view stays
/// decoupled from Application/PlayQueue wiring.
class LibraryView : public ventty::Widget
{
public:
    enum class Mode { AlbumArtistTree, Directory };

    void setTheme(Theme const & theme) { _theme = theme; }
    void setLibrary(MediaLibrary const * library);

    void setMode(Mode mode);
    Mode mode() const { return _mode; }

    /// Re-build the tree from the current library (call after a scan or root
    /// change). Preserves cursor position by best-effort path match.
    void rebuild();

    /// Drop the entire node tree without touching the library. Used while a
    /// background scan owns the MediaLibrary: `Node::track` points into the
    /// library's vector, which the scan reallocates, so the stale tree must
    /// be released before the worker runs. rebuild() restores it afterwards.
    void clear();

    /// Expand parents and move the cursor to the track node holding `path`.
    /// Drills all the way down to the track. No-op if `path` is not indexed.
    void locate(std::filesystem::path const & path);

    /// Like locate(), but in AlbumArtistTree mode stops at the
    /// album group that represents `path` (ancestors expanded, the album
    /// itself left folded) so switching axes lands on the album rather
    /// than always jumping to a track. Directory mode still drills to the
    /// track itself (folders expanded to reveal the file). Used when
    /// switching library modes.
    void locateForMode(std::filesystem::path const & path);

    /// Path of a track representing the current cursor: the selected track
    /// itself, or the first track under the selected group. Empty if there is
    /// no selection or no tracks under it. Used to carry focus across modes.
    std::filesystem::path selectedTrackPath() const;

    /// Fired when the user activates a track or group.
    /// `tracks` contains every track under the selection; `replace` is true
    /// for Enter (replace queue + play first), false for 'a' (append to bottom).
    using OnSendToQueue = std::function<void(std::vector<TrackInfo> tracks, bool replace)>;
    void setOnSendToQueue(OnSendToQueue cb) { _onSend = std::move(cb); }

    /// Fired when '/' is pressed.
    using OnSearch = std::function<void()>;
    void setOnSearch(OnSearch cb) { _onSearch = std::move(cb); }

    /// Collect every track under the current selection (artist / album /
    /// single track) and hand it to the OnSendToQueue callback. `replace`
    /// true clears the queue and plays; false appends to the bottom.
    void sendSelectionToQueue(bool replace);

    /// What the cursor is currently pointing at, in terms the tag editor
    /// needs to scope its UI (which fields to expose) and target (which
    /// tracks the edit applies to).
    enum class SelectionKind
    {
        None,           ///< no selection
        Grouping,       ///< AlbumArtistTree, depth-0 group (top-level "kpop", "jazz", ...)
        Artist,         ///< AlbumArtistTree, depth-1 group
        Album,          ///< AlbumArtistTree, depth-2 group
        DirectoryGroup, ///< Directory mode, any group (folder)
        Track,          ///< a single track leaf (any mode)
    };
    struct Selection
    {
        SelectionKind kind = SelectionKind::None;
        /// Display label for the dialog header — group name or track title.
        std::string label;
        /// Every track this selection covers. For a group, that's every
        /// track under the subtree; for a track, just the one.
        std::vector<TrackInfo> tracks;
    };
    Selection currentSelection() const;

    void draw(ventty::Window & window) override;
    bool handleKey(ventty::KeyEvent const & event) override;
    bool handleMouse(ventty::MouseEvent const & event);

protected:
    void onFocusChanged() override;

private:
    struct Node
    {
        enum class Kind { Group, Track };
        Kind kind = Kind::Group;
        std::string label;
        int  depth     = 0;
        bool expanded  = false;
        /// AlbumArtistTree only: this node's identity was *derived* from the
        /// `artist` tag because `albumArtist` was empty. A depth-1 artist node
        /// sets it when it has no real album-artist album (every track derived);
        /// a derived track — shown title-only directly under the artist (depth
        /// 2, album axis skipped) — sets it too. Rendered a shade darker.
        bool derived   = false;
        std::vector<std::size_t> children;   ///< indices into _nodes
        TrackInfo const * track = nullptr;   ///< Track only
        std::size_t parent = static_cast<std::size_t>(-1);
    };

    void buildDirectoryTree();
    void buildArtistAlbumTree();
    void sortNodeChildren();
    void recomputeVisible();
    void scrollToSelected();
    std::string nodeKey(std::size_t nodeIdx) const;
    void saveCurrentModeState();
    void restoreCurrentModeState();

    /// Append every track under `nodeIdx` (or the node itself if it's a Track).
    void collectTracks(std::size_t nodeIdx, std::vector<TrackInfo> & out) const;

    /// Expand the node and every ancestor. Returns true if state changed.
    bool expandPath(std::size_t nodeIdx);

    Theme _theme;
    MediaLibrary const * _library = nullptr;
    Mode _mode = Mode::AlbumArtistTree;

    std::vector<Node>        _nodes;
    std::vector<std::size_t> _roots;     ///< top-level node indices
    std::vector<std::size_t> _visible;   ///< render order

    int _selectedIndex = 0;              ///< index into _visible
    int _scrollOffset  = 0;

    struct ModeState
    {
        std::string selectedKey;
        std::vector<std::string> expandedKeys;
        int scrollOffset = 0;
        bool valid = false;
    };
    ModeState _albumState;
    ModeState _directoryState;

    /// Header count for the current mode (artists / albums / tracks),
    /// computed once per rebuild() rather than every draw().
    std::size_t _headerCount = 0;

    OnSendToQueue _onSend;
    OnSearch      _onSearch;
};

} // namespace vtplayer
