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
///   - Directory:    folder tree under the library root
///   - ArtistAlbum:  AlbumArtist > Album > Track (foobar2000-style)
/// Group nodes can be expanded/collapsed; only currently-visible rows are
/// rendered (virtual scrolling). Selection actions go out via callbacks so
/// the view stays decoupled from Application/PlayQueue wiring.
class LibraryView : public ventty::Widget
{
public:
    enum class Mode { Directory, ArtistAlbum };

    void setTheme(Theme const & theme) { _theme = theme; }
    void setLibrary(MediaLibrary const * library);

    void setMode(Mode mode);
    Mode mode() const { return _mode; }
    void toggleMode();

    /// Re-build the tree from the current library (call after a scan or root
    /// change). Preserves cursor position by best-effort path match.
    void rebuild();

    /// Expand parents and move the cursor to the node holding `path`. No-op
    /// if the path is not in the index.
    void locate(std::filesystem::path const & path);

    /// Fired when the user activates a track or group.
    /// `tracks` contains every track under the selection; `replace` is true
    /// for Enter (replace queue + play first), false for Shift+Enter (append).
    using OnSendToQueue = std::function<void(std::vector<TrackInfo> tracks, bool replace)>;
    void setOnSendToQueue(OnSendToQueue cb) { _onSend = std::move(cb); }

    /// Fired when '/' is pressed.
    using OnSearch = std::function<void()>;
    void setOnSearch(OnSearch cb) { _onSearch = std::move(cb); }

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
        std::vector<std::size_t> children;   ///< indices into _nodes
        TrackInfo const * track = nullptr;   ///< Track only
        std::size_t parent = static_cast<std::size_t>(-1);
    };

    void buildDirectoryTree();
    void buildArtistAlbumTree();
    void sortNodeChildren();
    void recomputeVisible();
    void scrollToSelected();

    /// Append every track under `nodeIdx` (or the node itself if it's a Track).
    void collectTracks(std::size_t nodeIdx, std::vector<TrackInfo> & out) const;

    /// Expand the node and every ancestor. Returns true if state changed.
    bool expandPath(std::size_t nodeIdx);

    Theme _theme;
    MediaLibrary const * _library = nullptr;
    Mode _mode = Mode::Directory;

    std::vector<Node>        _nodes;
    std::vector<std::size_t> _roots;     ///< top-level node indices
    std::vector<std::size_t> _visible;   ///< render order

    int _selectedIndex = 0;              ///< index into _visible
    int _scrollOffset  = 0;

    OnSendToQueue _onSend;
    OnSearch      _onSearch;
};

} // namespace vtplayer
