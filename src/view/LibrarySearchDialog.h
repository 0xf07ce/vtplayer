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

    /// Terminal-cell coordinates the host should park the hardware cursor
    /// at while this dialog owns input. -1 means "no cursor". Updated by
    /// draw(); read by the host run loop right after Terminal::render().
    bool wantsCursor() const { return _open && _cursorScreenX >= 0; }
    int cursorScreenX() const { return _cursorScreenX; }
    int cursorScreenY() const { return _cursorScreenY; }

private:
    /// Build `_haystack`: one pre-lowered, tab-joined searchable string per
    /// track. Done once when the dialog opens so each subsequent keystroke
    /// is a flat O(n) substring sweep over already-lowered text instead of
    /// repeatedly re-lowering five fields per track.
    void rebuildHaystack();
    void recomputeMatches();
    void insertUtf8(char32_t ch);
    void backspaceUtf8();
    void deleteForward();
    void moveCursorLeft();
    void moveCursorRight();
    void moveCursorHome();
    void moveCursorEnd();

    Theme _theme;
    MediaLibrary const * _library = nullptr;
    OnLocate _onLocate;

    bool _open = false;
    std::string _query;                              ///< UTF-8 query text
    int  _cursorBytePos = 0;                         ///< insertion point in _query
    std::vector<TrackInfo const *> _matches;
    int _selectedIndex = 0;
    int _scrollOffset  = 0;

    /// Pre-lowered haystack rebuilt by open(). Parallel to the snapshot of
    /// `_library->tracks()` taken at open time; matches() consults this
    /// rather than re-lowering five fields per track per keystroke.
    struct HaystackRow
    {
        TrackInfo const * track;
        std::string lower;
    };
    std::vector<HaystackRow> _haystack;

    int _cursorScreenX = -1;
    int _cursorScreenY = -1;
};

} // namespace vtplayer
