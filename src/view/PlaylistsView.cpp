// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PlaylistsView.h"

#include "../util/UnicodeNormalize.h"

#include <ventty/art/AsciiArt.h>
#include <ventty/core/Utf8.h>

#include <algorithm>

namespace vtplayer
{

using Key = ventty::KeyEvent::Key;

void PlaylistsView::setItems(std::vector<std::string> names)
{
    _names = std::move(names);
    if (_selectedIndex >= static_cast<int>(_names.size()))
        _selectedIndex = std::max(0, static_cast<int>(_names.size()) - 1);
    if (_scrollOffset > _selectedIndex)
        _scrollOffset = _selectedIndex;
}

void PlaylistsView::setReadOnly(bool readOnly)
{
    _readOnly = readOnly;
    if (_readOnly)
        _editMode = false;
}

void PlaylistsView::showContents(std::string name, std::vector<TrackInfo> tracks)
{
    _openName = std::move(name);
    _tracks = std::move(tracks);
    _inContents = true;
    _trackSel = 0;
    _trackScroll = 0;
    _editMode = false;
    clearTrackSelection();
}

void PlaylistsView::reloadContents(std::vector<TrackInfo> tracks)
{
    if (!_inContents) return;
    _tracks = std::move(tracks);
    clearTrackSelection();
    // Row 0 is "..", so the row count is tracks + 1. ensureVisible() re-clamps
    // the scroll on the next draw, so only the cursor needs clamping here.
    int const rowCount = static_cast<int>(_tracks.size()) + 1;
    _trackSel = std::clamp(_trackSel, 0, rowCount - 1);
}

void PlaylistsView::closeContents()
{
    // Leaving the contents view drops any unsaved edits (the discard path).
    _inContents = false;
    _openName.clear();
    _tracks.clear();
    _trackSel = 0;
    _trackScroll = 0;
    _editMode = false;
    clearTrackSelection();
}

std::string PlaylistsView::selectedName() const
{
    if (_inContents) return _openName;
    if (_names.empty()) return {};
    if (_selectedIndex < 0 || _selectedIndex >= static_cast<int>(_names.size()))
        return {};
    return _names[_selectedIndex];
}

TrackInfo const * PlaylistsView::selectedTrack() const
{
    if (!_inContents || _trackSel <= 0) return nullptr; // list view or ".." row
    int const ti = _trackSel - 1;
    if (ti < 0 || ti >= static_cast<int>(_tracks.size())) return nullptr;
    return &_tracks[ti];
}

std::vector<TrackInfo> PlaylistsView::selectedTracks() const
{
    if (!_inContents) return {};

    // Rows are 1-based over _tracks; union the multi-selection with the cursor
    // row, drop the ".." row (row 0), and emit in row order.
    std::set<int> rows = _trackMultiSel;
    if (_trackSel >= 1) rows.insert(_trackSel);

    std::vector<TrackInfo> out;
    out.reserve(rows.size());
    for (int row : rows)
    {
        int const ti = row - 1;
        if (ti >= 0 && ti < static_cast<int>(_tracks.size()))
            out.push_back(_tracks[ti]);
    }
    return out;
}

void PlaylistsView::clearTrackSelection()
{
    _trackMultiSel.clear();
    _trackAnchor = -1;
}

void PlaylistsView::onFocusChanged()
{
    if (!isFocused())
        clearTrackSelection();
}

void PlaylistsView::extendTrackSelectionTo(int newRow)
{
    if (_trackAnchor < 0) _trackAnchor = _trackSel;
    int const lo = std::min(_trackAnchor, newRow);
    int const hi = std::max(_trackAnchor, newRow);
    _trackMultiSel.clear();
    int const n = static_cast<int>(_tracks.size());
    for (int row = lo; row <= hi; ++row)
    {
        if (row >= 1 && row <= n) // skip row 0 (the ".." back-row)
            _trackMultiSel.insert(row);
    }
}

void PlaylistsView::selectAllTracks()
{
    // Select every track row (1..N); row 0 (the ".." back-row) is never
    // selectable. Anchor the shift-range at the cursor so a later Shift+arrow
    // extends from where the user is, mirroring FileBrowser / PlayQueueView.
    _trackMultiSel.clear();
    int const n = static_cast<int>(_tracks.size());
    for (int row = 1; row <= n; ++row)
        _trackMultiSel.insert(row);
    // Never leave the cursor on the ".." back-row here: it would be highlighted
    // as the cursor alongside every track, reading as if ".." were part of the
    // "select all". Park it on the first real track so ".." is excluded from
    // both the visual highlight and the logical selection.
    if (n > 0 && _trackSel == 0)
        _trackSel = 1;
    _trackAnchor = _trackSel;
}

void PlaylistsView::removeSelectedTracks()
{
    // Targets = multi-selection unioned with the cursor row, minus the ".."
    // row. Rows are 1-based over _tracks, so track index = row - 1.
    std::set<int> rows = _trackMultiSel;
    if (_trackSel >= 1) rows.insert(_trackSel);
    if (rows.empty()) return;

    int const firstRow = *rows.begin();

    // Erase high → low so earlier indices stay valid.
    for (auto it = rows.rbegin(); it != rows.rend(); ++it)
    {
        int const ti = *it - 1;
        if (ti >= 0 && ti < static_cast<int>(_tracks.size()))
            _tracks.erase(_tracks.begin() + ti);
    }

    int const rowCount = static_cast<int>(_tracks.size()) + 1; // ".." + tracks
    _trackSel = std::clamp(firstRow, 0, rowCount - 1);
    clearTrackSelection();
}

void PlaylistsView::moveTrackSelectionUp()
{
    int lo = _trackSel;
    int hi = _trackSel;
    if (!_trackMultiSel.empty())
    {
        lo = std::min(lo, *_trackMultiSel.begin());
        hi = std::max(hi, *_trackMultiSel.rbegin());
    }
    if (lo <= 1) return; // already at the top track row (row 1)

    // Rows are 1-based; track indices are row-1. Rotate the row above the block
    // down to the block's tail via adjacent swaps.
    for (int row = lo - 1; row < hi; ++row)
        std::swap(_tracks[row - 1], _tracks[row]);

    _trackSel -= 1;
    if (!_trackMultiSel.empty())
    {
        std::set<int> moved;
        for (int row : _trackMultiSel) moved.insert(row - 1);
        _trackMultiSel = std::move(moved);
    }
    if (_trackAnchor >= 0) _trackAnchor -= 1;
}

void PlaylistsView::moveTrackSelectionDown()
{
    int lo = _trackSel;
    int hi = _trackSel;
    if (!_trackMultiSel.empty())
    {
        lo = std::min(lo, *_trackMultiSel.begin());
        hi = std::max(hi, *_trackMultiSel.rbegin());
    }
    if (lo < 1) return;                                  // cursor on ".." row
    if (hi >= static_cast<int>(_tracks.size())) return;  // block at the bottom

    for (int row = hi + 1; row > lo; --row)
        std::swap(_tracks[row - 1], _tracks[row - 2]);

    _trackSel += 1;
    if (!_trackMultiSel.empty())
    {
        std::set<int> moved;
        for (int row : _trackMultiSel) moved.insert(row + 1);
        _trackMultiSel = std::move(moved);
    }
    if (_trackAnchor >= 0) _trackAnchor += 1;
}

bool PlaylistsView::saveEdits()
{
    if (_readOnly) return false;
    if (!_inContents || !_editMode) return false;
    // A failed write keeps edit mode on so the user can retry.
    bool const saved = !_onSaveTracks || _onSaveTracks(_openName, _tracks);
    if (saved)
    {
        _editMode = false;
        clearTrackSelection();
    }
    return saved;
}

void PlaylistsView::discardEdits(std::vector<TrackInfo> tracks)
{
    if (!_inContents) return;
    reloadContents(std::move(tracks)); // swap in the on-disk tracks, clamp cursor
    _editMode = false;                 // rollback complete — leave edit mode
}

void PlaylistsView::moveCursor(int delta)
{
    if (_inContents)
    {
        // Row 0 is "..", so the row count is tracks + 1 (always >= 1).
        int const n = static_cast<int>(_tracks.size()) + 1;
        _trackSel = std::clamp(_trackSel + delta, 0, n - 1);
        return;
    }
    if (_names.empty()) return;
    int const n = static_cast<int>(_names.size());
    _selectedIndex = std::clamp(_selectedIndex + delta, 0, n - 1);
}

void PlaylistsView::ensureVisible(int listH)
{
    if (listH <= 0) return;
    int & sel = _inContents ? _trackSel : _selectedIndex;
    int & scroll = _inContents ? _trackScroll : _scrollOffset;
    if (sel < scroll) scroll = sel;
    if (sel >= scroll + listH) scroll = sel - listH + 1;
    if (scroll < 0) scroll = 0;
}

bool PlaylistsView::handleKey(ventty::KeyEvent const & event)
{
    // Contents-view editing: Ctrl+E enters edit mode, Ctrl+S saves, and the
    // reorder / multi-select / delete keys are active only while editing.
    if (_inContents)
    {
        if (!_readOnly && event.key == Key::Char && event.ctrl &&
            (event.ch == 'e' || event.ch == 'E' || event.ch == 5))
        {
            // Ctrl+E only *enters* edit mode — it is not a toggle. Once editing,
            // it is ignored; leaving happens via Ctrl+S (save) or the ESC menu's
            // "Discard changes" (rollback). Swallow it either way.
            if (!_editMode) enterEditMode();
            return true;
        }
        if (!_readOnly && event.key == Key::Char && event.ctrl &&
            (event.ch == 's' || event.ch == 'S' || event.ch == 19))
        {
            saveEdits(); // save + leave edit mode (no-op when not editing)
            return true;
        }

        // Multi-selection and select-all work outside edit mode too: a
        // selection here can be sent to the play queue (Enter / `a`) without
        // entering the file-mutating edit mode. Row 0 (the ".." back-row) is
        // never selectable.
        // Ctrl+A selects every track row, matching FileBrowser / PlayQueue.
        if (event.key == Key::Char && event.ctrl &&
            (event.ch == 'a' || event.ch == 'A' || event.ch == 1))
        {
            selectAllTracks();
            return true;
        }
        // Shift+Up / Shift+Down extend the track multi-selection.
        if (event.key == Key::Up && event.shift)
        {
            if (_trackSel > 1)
            {
                if (_trackAnchor < 0) _trackAnchor = _trackSel;
                _trackSel -= 1;
                extendTrackSelectionTo(_trackSel);
            }
            return true;
        }
        if (event.key == Key::Down && event.shift)
        {
            if (_trackSel < static_cast<int>(_tracks.size()))
            {
                if (_trackAnchor < 0) _trackAnchor = _trackSel;
                _trackSel += 1;
                extendTrackSelectionTo(_trackSel);
            }
            return true;
        }

        // Reorder / delete mutate the saved playlist, so they stay gated behind
        // edit mode (toggled by Ctrl+E, persisted by Ctrl+S).
        if (!_readOnly && _editMode)
        {
            // Shift+Left / Shift+Right reorder the selected block (Left=up).
            if (event.key == Key::Left && event.shift) { moveTrackSelectionUp(); return true; }
            if (event.key == Key::Right && event.shift) { moveTrackSelectionDown(); return true; }
            // d / D / Delete remove the selection. Backspace is intentionally
            // NOT a delete key — it keeps its "go up to the list" meaning.
            if (event.key == Key::Delete ||
                (event.key == Key::Char && (event.ch == 'd' || event.ch == 'D')))
            {
                removeSelectedTracks();
                return true;
            }
        }
    }

    if (event.key == Key::Up) { if (_inContents) clearTrackSelection(); moveCursor(-1); return true; }
    if (event.key == Key::Down) { if (_inContents) clearTrackSelection(); moveCursor(+1); return true; }
    if (event.key == Key::PageUp) { if (_inContents) clearTrackSelection(); moveCursor(-8); return true; }
    if (event.key == Key::PageDown) { if (_inContents) clearTrackSelection(); moveCursor(+8); return true; }
    if (event.key == Key::Home)
    {
        if (_inContents) clearTrackSelection();
        (_inContents ? _trackSel : _selectedIndex) = 0;
        return true;
    }
    if (event.key == Key::End)
    {
        if (_inContents)
        {
            clearTrackSelection();
            _trackSel = static_cast<int>(_tracks.size()); // last track row
        }
        else
            _selectedIndex = std::max(0, static_cast<int>(_names.size()) - 1);
        return true;
    }

    if (_inContents)
    {
        // Backspace: in edit mode it removes the selection (an alias for `d`),
        // since destructive edits are the focus there; otherwise it mirrors
        // FileBrowser's "go up a level".
        if (event.key == Key::Backspace)
        {
            if (!_readOnly && _editMode) removeSelectedTracks();
            else closeContents();
            return true;
        }
        if (event.key == Key::Enter)
        {
            if (_trackSel == 0) { closeContents(); return true; } // ".." row
            // Replace the queue with the whole selection (multi-selection ∪
            // cursor); a bare cursor yields just that one track.
            if (_onPlayTracks)
            {
                if (auto tracks = selectedTracks(); !tracks.empty())
                    _onPlayTracks(tracks);
            }
            return true;
        }
        return false;
    }

    if (event.key == Key::Enter)
    {
        // Drill into the selected playlist; the host reads its tracks and
        // calls showContents().
        std::string const name = selectedName();
        if (_onOpen && !name.empty())
            _onOpen(name);
        return true;
    }
    return false;
}

bool PlaylistsView::handleMouse(ventty::MouseEvent const & event)
{
    auto const & r = rect();
    if (!r.contains(event.x, event.y))
        return false;

    using Button = ventty::MouseEvent::Button;
    using Action = ventty::MouseEvent::Action;

    int const listH = r.height - 2;

    // Wheel scroll moves the cursor one row (same feel as FileBrowser).
    if (event.button == Button::ScrollUp) { moveCursor(-1); ensureVisible(listH); return true; }
    if (event.button == Button::ScrollDown) { moveCursor(+1); ensureVisible(listH); return true; }

    if (event.button == Button::Left && event.action == Action::Press)
    {
        int const listY = r.y + 2; // rows start after header + separator
        if (event.y < listY)
            return true;
        int const clickedRow = event.y - listY;

        if (_inContents)
        {
            int const rowCount = static_cast<int>(_tracks.size()) + 1; // ".." + tracks
            int const idx = _trackScroll + clickedRow;
            if (idx < 0 || idx >= rowCount)
                return true;
            if (_trackSel == idx)
            {
                // Second click on the focused row activates it (mirrors the
                // FileBrowser double-click effect).
                if (idx == 0)
                    closeContents(); // ".." row
                else if (_onPlayTracks)
                {
                    if (auto tracks = selectedTracks(); !tracks.empty())
                        _onPlayTracks(tracks);
                }
            }
            else
            {
                _trackSel = idx;
            }
        }
        else
        {
            int const idx = _scrollOffset + clickedRow;
            if (idx < 0 || idx >= static_cast<int>(_names.size()))
                return true;
            if (_selectedIndex == idx)
            {
                std::string const name = selectedName();
                if (_onOpen && !name.empty())
                    _onOpen(name);
            }
            else
            {
                _selectedIndex = idx;
            }
        }
        return true;
    }
    return false;
}

void PlaylistsView::draw(ventty::Window & window)
{
    auto const & r = rect();
    if (r.width <= 0 || r.height <= 0) return;

    // Left border
    for (int y = 0; y < r.height; ++y)
    {
        window.putChar(r.x, r.y + y, ventty::DOUBLE_BOX.v,
                       ventty::Style{_theme.border, _theme.browserBg});
    }

    // Header — the playlist name in contents view, otherwise "Playlists".
    ventty::Style headerTextStyle{_theme.browserHeaderFg, _theme.browserBg, ventty::Attr::Bold};
    window.fill(r.x + 1, r.y, r.width - 1, 1, U' ', headerTextStyle);
    // In the contents view, surface "[edit]" while edit mode is armed.
    std::string const suffix = (_inContents && _editMode) ? " [edit]" : "";
    std::string const title =
        _inContents ? (" " + _openName + suffix) : (" " + _title);
    std::string header = truncateToWidth(title, r.width - 2, "...");
    window.drawText(r.x + 1, r.y, header, headerTextStyle);

    // Separator line
    ventty::Style sepStyle{_theme.border, _theme.browserBg};
    for (int x = r.x + 1; x < r.x + r.width; ++x)
    {
        window.putChar(x, r.y + 1, ventty::HR_THIN, sepStyle);
    }

    if (_inContents)
        drawContents(window);
    else
        drawList(window);
}

void PlaylistsView::drawList(ventty::Window & window)
{
    auto const & r = rect();
    int const listH = r.height - 2;
    int const contentW = r.width - 2;

    // Empty-state hint.
    if (_names.empty())
    {
        ventty::Style hintStyle{_theme.separatorFg, _theme.browserBg};
        std::string hint = truncateToWidth(_emptyHint, contentW, "...");
        window.drawText(r.x + 1, r.y + 2, hint, hintStyle);
        return;
    }

    ensureVisible(listH);

    for (int i = 0; i < listH; ++i)
    {
        int const idx = _scrollOffset + i;
        int const y = r.y + 2 + i;

        if (idx >= static_cast<int>(_names.size()))
        {
            window.fill(r.x + 1, y, r.width - 1, 1, U' ',
                        ventty::Style{_theme.browserFg, _theme.browserBg});
            continue;
        }

        bool const cursor = (idx == _selectedIndex) && isFocused();
        Color const fg = cursor ? _theme.browserSelFg : _theme.browserFg;
        Color const bg = cursor ? _theme.browserSelBg : _theme.browserBg;
        ventty::Style style{fg, bg};
        window.fill(r.x + 1, y, r.width - 1, 1, U' ', style);

        std::string icon = " \xE2\x89\xA1 "; // ≡
        window.drawText(r.x + 1, y, icon, style);
        int const textX = r.x + 1 + static_cast<int>(ventty::stringWidth(icon));

        std::string name = truncateToWidth(_names[idx], contentW - 4, "...");
        window.drawText(textX, y, name, style);
    }
}

void PlaylistsView::drawContents(ventty::Window & window)
{
    auto const & r = rect();
    int const listH = r.height - 2;
    int const contentW = r.width - 2;

    ensureVisible(listH);

    // Row 0 is the ".." back-row; tracks occupy rows 1..N.
    int const rowCount = static_cast<int>(_tracks.size()) + 1;

    for (int i = 0; i < listH; ++i)
    {
        int const row = _trackScroll + i;
        int const y = r.y + 2 + i;

        if (row >= rowCount)
        {
            window.fill(r.x + 1, y, r.width - 1, 1, U' ',
                        ventty::Style{_theme.browserFg, _theme.browserBg});
            continue;
        }

        bool const cursor = (row == _trackSel) && isFocused();
        bool const multi  = isFocused() && _trackMultiSel.count(row) > 0;
        bool const isBack = (row == 0);
        Color fg;
        if (cursor || multi)
            fg = _theme.browserSelFg;
        else if (isBack)
            fg = _theme.browserDirFg;
        else
            fg = _theme.browserAudioFg;
        Color const bg = (cursor || multi) ? _theme.browserSelBg : _theme.browserBg;
        ventty::Style style{fg, bg};
        window.fill(r.x + 1, y, r.width - 1, 1, U' ', style);

        std::string icon = isBack ? " \xE2\x86\x90 "  // ←
                                  : " \xE2\x99\xAA "; // ♪
        window.drawText(r.x + 1, y, icon, style);
        int const textX = r.x + 1 + static_cast<int>(ventty::stringWidth(icon));

        std::string label;
        if (isBack)
        {
            label = "..";
        }
        else
        {
            TrackInfo const & t = _tracks[row - 1];
            std::string const name = !t.title.empty() ? t.title : t.path.filename().string();
            label = t.artist.empty() ? name : (t.artist + " - " + name);
        }
        std::string name = truncateToWidth(label, contentW - 4, "...");
        window.drawText(textX, y, name, style);
    }
}

} // namespace vtplayer
