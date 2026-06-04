// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Theme.h"

#include <ventty/core/Window.h>
#include <ventty/terminal/TerminalBase.h>

#include <functional>
#include <optional>
#include <string>

namespace vtplayer
{

/// Modal single-line text-entry overlay (e.g. "name this playlist"). A plain
/// class driven by the host run loop, mirroring LibrarySearchDialog's
/// open/close/handleKey/draw/wantsCursor contract. UTF-8 aware.
///
/// Enter fires the OnConfirm callback with the trimmed text (only when
/// non-empty). The callback returns an optional error message: nullopt/empty
/// closes the dialog (success), while a non-empty string keeps it open and
/// shows that message below the field — used to reject e.g. a name collision
/// and let the user edit and retry. ESC always closes without firing.
class TextInputDialog
{
public:
    /// Return an error to keep the dialog open (shown to the user), or
    /// nullopt on success (closes the dialog).
    using OnConfirm = std::function<std::optional<std::string>(std::string const & text)>;

    void setTheme(Theme const & theme) { _theme = theme; }
    void setOnConfirm(OnConfirm cb) { _onConfirm = std::move(cb); }

    void open(std::string title, std::string prompt, std::string initial = {});
    void close();
    bool isOpen() const { return _open; }

    bool handleKey(ventty::KeyEvent const & event);
    void draw(ventty::Window & window);

    bool wantsCursor() const { return _open && _cursorScreenX >= 0; }
    int cursorScreenX() const { return _cursorScreenX; }
    int cursorScreenY() const { return _cursorScreenY; }

private:
    void insertUtf8(char32_t ch);
    void backspaceUtf8();
    void deleteForward();
    void moveCursorLeft();
    void moveCursorRight();

    Theme _theme;
    OnConfirm _onConfirm;

    bool _open = false;
    std::string _title;
    std::string _prompt;
    std::string _text;        ///< UTF-8 buffer
    std::string _error;       ///< validation hint shown below the field; cleared on edit
    int _cursorBytePos = 0;   ///< insertion point in _text

    int _cursorScreenX = -1;
    int _cursorScreenY = -1;
};

} // namespace vtplayer
