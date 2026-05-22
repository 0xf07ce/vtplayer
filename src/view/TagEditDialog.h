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

/// Modal "edit tags" overlay. Centered, mirrors LibrarySearchDialog style.
/// The set of editable fields depends on Scope:
///   - Artist:          artist (writes both ARTIST and ALBUMARTIST)
///   - Album:           artist, albumArtist, album, genre, disc, year
///                      (title / track number stay per-track)
///   - SingleTrack:     every tag field
///   - MultiTrack:      every field, but empty == "leave unchanged" — a
///                      field is only written when the user actually edits
///                      it (the dirty flag flips on first keystroke).
class TagEditDialog
{
public:
    enum class Scope
    {
        Artist,
        Album,
        SingleTrack,
        MultiTrack,
    };

    /// Hand-back from a successful save. `targets` is the path list passed
    /// in by open(); `update` is the sparse change to apply to each.
    using OnSave =
        std::function<void(std::vector<std::filesystem::path> const & targets,
                           TagUpdate const & update)>;

    void setTheme(Theme const & theme) { _theme = theme; }
    void setOnSave(OnSave cb) { _onSave = std::move(cb); }

    /// Open the dialog. `tracks` is the full list of tracks to edit (used
    /// to seed initial field values — common values shown, mixed values
    /// shown as "<various>"). `header` is the title strip text.
    void open(Scope scope,
              std::string header,
              std::vector<TrackInfo> tracks);
    void close();
    bool isOpen() const { return _open; }

    bool handleKey(ventty::KeyEvent const & event);
    void draw(ventty::Window & window);

    /// Terminal-cell coordinates where the hardware cursor should sit
    /// while this dialog owns input. -1 means "no cursor". Updated by
    /// draw(); read by the host run loop right after Terminal::render()
    /// so the cursor lands on top of the latest cell output.
    bool wantsCursor() const { return _open && _cursorScreenX >= 0; }
    int cursorScreenX() const { return _cursorScreenX; }
    int cursorScreenY() const { return _cursorScreenY; }

private:
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
    void commit();

    Theme _theme;
    OnSave _onSave;

    bool _open = false;
    Scope _scope = Scope::SingleTrack;
    std::string _header;
    std::vector<TrackInfo> _tracks;
    std::vector<std::filesystem::path> _targetPaths;

    std::vector<Field> _fields;
    int _focusedField = 0;

    int _cursorScreenX = -1;
    int _cursorScreenY = -1;
};

} // namespace vtplayer
