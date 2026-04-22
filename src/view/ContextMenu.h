// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Theme.h"

#include <ventty/core/Window.h>
#include <ventty/terminal/TerminalBase.h>

#include <functional>
#include <string>
#include <vector>

namespace vtplayer
{

/// Modal overlay with a bordered, centered list of selectable actions.
/// Opened by ESC; items are plain English labels provided by the host.
class ContextMenu
{
public:
    using SelectCallback = std::function<void(int index)>;

    void setTheme(Theme const & theme) { _theme = theme; }
    void setTitle(std::string title) { _title = std::move(title); }
    void setItems(std::vector<std::string> items);
    void setOnSelect(SelectCallback cb) { _onSelect = std::move(cb); }

    void open();
    void close();
    bool isOpen() const { return _open; }

    /// Returns true if the event was consumed by the menu.
    bool handleKey(ventty::KeyEvent const & event);

    /// Draws the menu centered in the given root window.
    void draw(ventty::Window & window);

private:
    Theme _theme;
    std::string _title = "Menu";
    std::vector<std::string> _items;
    SelectCallback _onSelect;
    int _selected = 0;
    bool _open = false;
};

} // namespace vtplayer
