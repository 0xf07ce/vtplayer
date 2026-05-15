// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Theme.h"
#include "../audio/TrackInfo.h"

#include <ventty/core/Window.h>
#include <ventty/terminal/TerminalBase.h>

#include <functional>
#include <string>
#include <vector>

namespace vtplayer
{

class MediaLibrary;

/// Modal search overlay backed by the in-memory MediaLibrary index.
/// One-line query field on top, live-filtered results below. Matching is
/// case-insensitive substring on title / artist / album / albumArtist /
/// genre. Enter on a result replaces the play queue and starts playback;
/// Shift+Enter appends. ESC closes.
class LibrarySearchDialog
{
public:
    using OnSendToQueue = std::function<void(std::vector<TrackInfo> tracks, bool replace)>;

    void setTheme(Theme const & theme) { _theme = theme; }
    void setLibrary(MediaLibrary const * library) { _library = library; }
    void setOnSendToQueue(OnSendToQueue cb) { _onSend = std::move(cb); }

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
    OnSendToQueue _onSend;

    bool _open = false;
    std::string _query;                              ///< UTF-8 query text
    std::vector<TrackInfo const *> _matches;
    int _selectedIndex = 0;
    int _scrollOffset  = 0;
};

} // namespace vtplayer
