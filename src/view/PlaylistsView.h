// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Theme.h"

#include <ventty/widget/Widget.h>

#include <functional>
#include <string>
#include <vector>

namespace vtplayer
{

/// Left-panel browser listing saved playlists by name (mode 5). It holds no
/// disk logic: the host feeds it names via setItems() after querying
/// PlaylistStore. Selection / scroll live here. Creating and deleting
/// playlists is driven from the host's ESC menu, not from this widget.
class PlaylistsView : public ventty::Widget
{
public:
    /// Fired when the user activates a playlist (Enter). Unused in the first
    /// cut — wired later to load the playlist into the play queue.
    using OnActivate = std::function<void(std::string const & name)>;

    void setTheme(Theme const & theme) { _theme = theme; }
    void setOnActivate(OnActivate cb) { _onActivate = std::move(cb); }

    /// Replace the displayed names (caller supplies them already sorted).
    /// Clamps selection / scroll so deleting the last row stays valid.
    void setItems(std::vector<std::string> names);

    /// Name under the cursor, or "" when the list is empty.
    std::string selectedName() const;
    bool empty() const { return _names.empty(); }

    void draw(ventty::Window & window) override;
    bool handleKey(ventty::KeyEvent const & event) override;

private:
    void moveCursor(int delta);
    void ensureVisible(int listH);

    Theme _theme;
    std::vector<std::string> _names;
    int _selectedIndex = 0;
    int _scrollOffset = 0;
    OnActivate _onActivate;
};

} // namespace vtplayer
