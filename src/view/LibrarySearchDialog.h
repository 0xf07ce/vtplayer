// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Theme.h"
#include "../audio/TrackInfo.h"

#include <ventty/core/Window.h>
#include <ventty/terminal/TerminalBase.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace vtplayer
{

class MediaLibrary;

/// Modal search overlay backed by the in-memory MediaLibrary index.
/// One-line query field on top, live-filtered results below. Matching is
/// case-insensitive substring on title / artist / album / albumArtist /
/// genre. Enter on a result closes the dialog and locates that track in
/// the library tree (cursor moves there, parents expand) — it does not
/// touch the play queue or playback. ESC closes without locating.
class LibrarySearchDialog
{
public:
    /// Fired with the chosen track's path when the user presses Enter on a
    /// result. The host moves the LibraryView cursor to it.
    using OnLocate = std::function<void(std::filesystem::path const & path)>;

    void setTheme(Theme const & theme) { _theme = theme; }
    void setLibrary(MediaLibrary const * library) { _library = library; }
    void setOnLocate(OnLocate cb) { _onLocate = std::move(cb); }

    void open();
    void close();
    bool isOpen() const { return _open; }

    /// Returns true if the event was consumed.
    bool handleKey(ventty::KeyEvent const & event);

    /// Renders centered in the given root window.
    void draw(ventty::Window & window);

private:
    void recomputeMatches();
    void appendUtf8(char32_t ch);
    void backspaceUtf8();

    Theme _theme;
    MediaLibrary const * _library = nullptr;
    OnLocate _onLocate;

    bool _open = false;
    std::string _query;                              ///< UTF-8 query text
    std::vector<TrackInfo const *> _matches;
    int _selectedIndex = 0;
    int _scrollOffset  = 0;
};

} // namespace vtplayer
