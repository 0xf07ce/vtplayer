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
/// case-insensitive substring; the field set is selected by a tab bar at
/// the top of the dialog (Any / Artist / Album / Title / Year), cycled
/// with Tab and Shift+Tab. Enter on a result closes the dialog and locates
/// that track in the library tree (cursor moves there, parents expand) —
/// it does not touch the play queue or playback. ESC closes without
/// locating.
///
/// The result list survives a close so the host can step through hits
/// with `navigateNext()` / `navigatePrev()` (driven by `n` / `N` in the
/// library panel, vim-style). `invalidateNav()` must be called whenever
/// the library is rebuilt to drop the now-stale snapshot.
class LibrarySearchDialog
{
public:
    /// Which fields the query is matched against. `Any` ORs every searchable
    /// field together (the historical default); the others narrow to one.
    enum class Filter
    {
        Any,
        Artist,
        Album,
        Title,
        Year,
    };

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

    /// Step to the next / previous saved match and re-fire `_onLocate`.
    /// Returns true iff a result was located. Used by the host's `n` / `N`
    /// keys outside the dialog (vim-style search navigation).
    bool navigateNext();
    bool navigatePrev();
    bool hasNav() const { return !_navPaths.empty(); }

    /// Drop the saved match snapshot. Call when the library index changes
    /// (rescan, root switch) so `n` / `N` don't locate paths that may have
    /// vanished from the tree.
    void invalidateNav();

private:
    /// Build `_haystack`: per-track pre-lowered fields plus a tab-joined
    /// "any" blob. Done once when the dialog opens so each subsequent
    /// keystroke is a flat O(n) substring sweep over already-lowered text.
    void rebuildHaystack();
    void recomputeMatches();
    void cycleFilter(int dir);
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
    Filter _filter = Filter::Any;                    ///< Persists across opens
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
        std::string any;
        std::string artist;
        std::string album;
        std::string title;
        std::string year;
    };
    std::vector<HaystackRow> _haystack;

    /// Persistent snapshot for `n` / `N` navigation outside the dialog.
    /// Stored as paths (not pointers) so that a library rebuild between
    /// close() and the next nav doesn't dangle — the host re-resolves the
    /// path through LibraryView::locate.
    std::vector<std::filesystem::path> _navPaths;
    int _navIndex = -1;

    int _cursorScreenX = -1;
    int _cursorScreenY = -1;
};

} // namespace vtplayer
