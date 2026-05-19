// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Theme.h"
#include "../util/StreamList.h"

#include <ventty/widget/Widget.h>

#include <functional>
#include <string>
#include <vector>

namespace vtplayer
{

/// Left-panel list of internet-radio streams loaded from `streams.m3u`.
/// Enter (or double-click) starts the selected stream. Shares the left-panel
/// slot with FileBrowser / LibraryView and matches their framing.
class RadioView : public ventty::Widget
{
public:
    void setTheme(Theme const & theme) { _theme = theme; }
    void setStreams(std::vector<Stream> streams);

    /// Index of the stream currently playing (highlighted), or -1.
    void setPlayingIndex(int idx) { _playingIndex = idx; }

    using OnPlay = std::function<void(Stream const &)>;
    void setOnPlay(OnPlay cb) { _onPlay = std::move(cb); }

    void draw(ventty::Window & window) override;
    bool handleKey(ventty::KeyEvent const & event) override;
    bool handleMouse(ventty::MouseEvent const & event);

private:
    void scrollToSelected();

    Theme _theme;
    std::vector<Stream> _streams;
    int _selectedIndex = 0;
    int _scrollOffset  = 0;
    int _playingIndex  = -1;
    OnPlay _onPlay;
};

} // namespace vtplayer
