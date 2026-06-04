// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Theme.h"

#include <ventty/core/Window.h>
#include <ventty/terminal/TerminalBase.h>

#include <functional>
#include <string>

namespace vtplayer
{

/// Generic modal yes/no confirmation overlay. A plain class driven by the
/// host run loop, sharing the open/close/handleKey/draw/wantsCursor contract
/// with the other modals. Left/Right/Tab flip the choice; y/n are hotkeys;
/// Enter fires OnConfirm with the chosen bool; ESC closes without firing (a
/// cancel — nothing happens).
class ConfirmDialog
{
public:
    using OnConfirm = std::function<void(bool yes)>;

    void setTheme(Theme const & theme) { _theme = theme; }
    void setOnConfirm(OnConfirm cb) { _onConfirm = std::move(cb); }

    void open(std::string title, std::string message, bool defaultYes = false);
    void close();
    bool isOpen() const { return _open; }

    bool handleKey(ventty::KeyEvent const & event);
    void draw(ventty::Window & window);

    // No text cursor; kept for uniform host plumbing.
    bool wantsCursor() const { return false; }
    int cursorScreenX() const { return -1; }
    int cursorScreenY() const { return -1; }

private:
    Theme _theme;
    OnConfirm _onConfirm;
    bool _open = false;
    bool _yes = false;
    std::string _title;
    std::string _message;
};

} // namespace vtplayer
