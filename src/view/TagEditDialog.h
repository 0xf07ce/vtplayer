// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Theme.h"
#include "../audio/TrackInfo.h"
#include "../util/TagWriter.h"

#include <ventty/core/Window.h>
#include <ventty/terminal/TerminalBase.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace vtplayer
{

/// Modal "tags" overlay. Always shows the same set of fields regardless of
/// what the caller selected — UX consistency wins over scope-specific
/// trimming. The dialog has three modes:
///   - View:         title is "Tags"; fields are read-only. Ctrl+E enters
///                   Edit mode. ESC closes. Arrow keys are inert — there is
///                   nothing to focus when nothing is editable.
///   - Edit:         title is "Edit Tags"; fields accept text input.
///                   Ctrl+S brings up a save confirmation. ESC reverts all
///                   pending edits and drops back to View (a second ESC then
///                   closes); nothing is written until the user confirms.
///   - ConfirmSave:  Yes/No overlay on top of the editor. Yes invokes
///                   _onSave with a sparse update built from the fields
///                   the user actually touched (the dirty flag); No
///                   returns to Edit mode.
///
/// Fields the user never typed in stay sparse — they are *not* written, so
/// loading a multi-track selection with mixed values and editing only one
/// row will not clobber the others.
class TagEditDialog
{
public:
    /// Hand-back from a confirmed save. `targets` is the path list passed
    /// in by open(); `update` is the sparse change to apply to each.
    using OnSave =
        std::function<void(std::vector<std::filesystem::path> const & targets,
                           TagUpdate const & update)>;

    void setTheme(Theme const & theme) { _theme = theme; }
    void setOnSave(OnSave cb) { _onSave = std::move(cb); }

    /// Open the dialog over `tracks`. `header` is the sub-title shown
    /// below the main title strip — used to describe what's being viewed
    /// (e.g. "Album: Kid A  (12 tracks)").
    void open(std::string header, std::vector<TrackInfo> tracks);
    void close();
    bool isOpen() const { return _open; }

    bool handleKey(ventty::KeyEvent const & event);
    void draw(ventty::Window & window);

    /// Terminal-cell coordinates where the hardware cursor should sit
    /// while this dialog owns input. -1 means "no cursor". Only the Edit
    /// mode parks a cursor; View mode hides it. Updated by draw().
    bool wantsCursor() const { return _open && _cursorScreenX >= 0; }
    int cursorScreenX() const { return _cursorScreenX; }
    int cursorScreenY() const { return _cursorScreenY; }

private:
    enum class Mode
    {
        View,
        Edit,
        ConfirmSave,
    };

    struct Field
    {
        std::string label;       ///< display label, e.g. "Artist:"
        std::string value;       ///< UTF-8 buffer (what user sees / edits)
        std::string placeholder; ///< shown dim when value is empty + !dirty
        bool numeric = false;    ///< restrict input to digits (track/disc/year)
        bool dirty   = false;    ///< true once the user has typed in this field
        char const * key = "";   ///< stable identifier — drives the TagUpdate
        int  cursorPos = 0;      ///< byte offset into `value`; per-field
                                 ///< so focus moves don't reset it
    };

    void buildFields();
    void insertUtf8(char32_t ch);
    void backspaceUtf8();
    void deleteForward();
    void moveCursorLeft();
    void moveCursorRight();
    void moveCursorHome();
    void moveCursorEnd();

    /// Assemble a sparse TagUpdate from the dirty fields.
    TagUpdate buildUpdate() const;
    /// True if at least one field has been touched in this Edit session.
    bool hasEdits() const;

    void drawEditor(ventty::Window & window, int x, int y, int dlgW, int dlgH);
    void drawConfirm(ventty::Window & window);

    Theme _theme;
    OnSave _onSave;

    bool _open = false;
    Mode _mode = Mode::View;
    std::string _header;
    std::vector<TrackInfo> _tracks;
    std::vector<std::filesystem::path> _targetPaths;

    std::vector<Field> _fields;
    int _focusedField = 0;

    /// Default the confirm overlay to Yes — the user just pressed
    /// Ctrl+S, so making Enter mean "go ahead and save" matches intent.
    bool _confirmYes = true;

    int _cursorScreenX = -1;
    int _cursorScreenY = -1;
};

} // namespace vtplayer
