// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Theme.h"
#include "../audio/TrackInfo.h"

#include <ventty/widget/Widget.h>

#include <functional>
#include <string>
#include <vector>

namespace vtamp
{

class PlaylistView : public ventty::Widget
{
public:
    void setTheme(Theme const & theme) { _theme = theme; }

    void addTrack(TrackInfo const & track);
    void removeSelected();
    void moveSelectedUp();
    void moveSelectedDown();
    void clear();

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

    void draw(ventty::Window & window) override;
    bool handleKey(ventty::KeyEvent const & event) override;
    bool handleMouse(ventty::MouseEvent const & event);

private:
    void scrollToSelected();

    Theme _theme;
    std::vector<TrackInfo> _tracks;
    int _selectedIndex = 0;
    int _scrollOffset = 0;
    int _playingIndex = -1;
    OnPlayCallback _onPlay;
};

} // namespace vtamp
