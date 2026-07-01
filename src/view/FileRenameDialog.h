// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Theme.h"
#include "../audio/TrackInfo.h"

#include <ventty/core/Window.h>
#include <ventty/terminal/TerminalBase.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vtplayer
{

/// Modal single-file rename overlay. F2 applies, ESC cancels. Suggestions are
/// generated from the file's real tags; missing tag parts show unavailable.
class FileRenameDialog
{
public:
    using OnConfirm =
        std::function<std::optional<std::string>(std::filesystem::path const & path,
                                                 std::string const & newName)>;

    void setTheme(Theme const & theme) { _theme = theme; }
    void setOnConfirm(OnConfirm cb) { _onConfirm = std::move(cb); }

    void open(TrackInfo track);
    void close();
    bool isOpen() const { return _open; }

    bool handleKey(ventty::KeyEvent const & event);
    void draw(ventty::Window & window);

    bool wantsCursor() const { return _open && _cursorScreenX >= 0; }
    int cursorScreenX() const { return _cursorScreenX; }
    int cursorScreenY() const { return _cursorScreenY; }

private:
    void buildSuggestions();
    bool suggestionEnabled(int idx) const;
    void moveFocus(int direction);
    void applySuggestion();
    void save();

    void insertUtf8(char32_t ch);
    void backspaceUtf8();
    void deleteForward();
    void moveCursorLeft();
    void moveCursorRight();

    Theme _theme;
    OnConfirm _onConfirm;

    bool _open = false;
    TrackInfo _track;
    std::string _name;
    std::string _error;
    int _cursorBytePos = 0;
    int _focus = 0; // 0 = editable name, 1..4 = suggestions
    std::vector<std::pair<std::string, std::string>> _suggestions;
    std::vector<bool> _suggestionEnabled;

    int _cursorScreenX = -1;
    int _cursorScreenY = -1;
};

} // namespace vtplayer
