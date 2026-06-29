// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "TagEditDialog.h"

#include "../util/UnicodeNormalize.h"

#include <ventty/art/AsciiArt.h>
#include <ventty/core/Utf8.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>

namespace vtplayer
{

namespace
{

using Key = ventty::KeyEvent::Key;

/// Marker that flips a field to "mixed values" display. Never written.
constexpr char const * kVariousPlaceholder = "<various>";

std::string intOrEmpty(int v)
{
    return v > 0 ? std::to_string(v) : std::string{};
}

int parseIntOrZero(std::string const & s)
{
    if (s.empty()) return 0;
    try
    {
        return std::stoi(s);
    }
    catch (...)
    {
        return 0;
    }
}

/// Common value across all tracks for the string field selected by `pick`;
/// empty string if all tracks share the empty value; nullopt if they differ.
template <typename F>
std::optional<std::string> commonString(std::vector<TrackInfo> const & tracks, F pick)
{
    if (tracks.empty()) return std::string{};
    std::string const & first = pick(tracks[0]);
    for (auto const & t : tracks)
    {
        if (pick(t) != first) return std::nullopt;
    }
    return first;
}

template <typename F>
std::optional<int> commonInt(std::vector<TrackInfo> const & tracks, F pick)
{
    if (tracks.empty()) return 0;
    int const first = pick(tracks[0]);
    for (auto const & t : tracks)
    {
        if (pick(t) != first) return std::nullopt;
    }
    return first;
}

} // namespace

void TagEditDialog::open(std::string header,
                         std::vector<TrackInfo> tracks,
                         bool readOnly)
{
    _open = true;
    _mode = Mode::View;
    _readOnly = readOnly;
    _header = std::move(header);
    _tracks = std::move(tracks);
    _targetPaths.clear();
    _targetPaths.reserve(_tracks.size());
    for (auto const & t : _tracks)
        _targetPaths.push_back(t.path);

    buildFields();
    _focusedField = 0;
    _confirmYes = true;
}

void TagEditDialog::close()
{
    _open = false;
    _mode = Mode::View;
    _tracks.clear();
    _targetPaths.clear();
    _fields.clear();
}

void TagEditDialog::buildFields()
{
    _fields.clear();

    auto strField = [&](char const * label, char const * key,
                        std::optional<std::string> const & common)
    {
        Field f;
        f.label = label;
        f.key = key;
        if (common)
        {
            f.value = *common;
            f.placeholder.clear();
        }
        else
        {
            f.placeholder = kVariousPlaceholder;
        }
        f.cursorPos = static_cast<int>(f.value.size());
        _fields.push_back(std::move(f));
    };

    auto intField = [&](char const * label, char const * key,
                        std::optional<int> const & common)
    {
        Field f;
        f.label = label;
        f.key = key;
        f.numeric = true;
        if (common)
        {
            f.value = intOrEmpty(*common);
            f.placeholder.clear();
        }
        else
        {
            f.placeholder = kVariousPlaceholder;
        }
        f.cursorPos = static_cast<int>(f.value.size());
        _fields.push_back(std::move(f));
    };

    // Unified field set — same regardless of how the dialog was opened.
    // Empty fields the user never touches stay sparse and won't be written.
    strField("Title:",        "title",
             commonString(_tracks, [](TrackInfo const & t) -> std::string const & { return t.title; }));
    intField("Track #:",      "trackNumber",
             commonInt(_tracks, [](TrackInfo const & t) { return t.trackNumber; }));
    strField("Artist:",       "artist",
             commonString(_tracks, [](TrackInfo const & t) -> std::string const & { return t.artist; }));
    strField("Album Artist:", "albumArtist",
             commonString(_tracks, [](TrackInfo const & t) -> std::string const & { return t.albumArtist; }));
    strField("Album:",        "album",
             commonString(_tracks, [](TrackInfo const & t) -> std::string const & { return t.album; }));
    strField("Genre:",        "genre",
             commonString(_tracks, [](TrackInfo const & t) -> std::string const & { return t.genre; }));
    strField("Grouping:",     "grouping",
             commonString(_tracks, [](TrackInfo const & t) -> std::string const & { return t.grouping; }));
    intField("Disc:",         "discNumber",
             commonInt(_tracks, [](TrackInfo const & t) { return t.discNumber; }));
    intField("Year:",         "year",
             commonInt(_tracks, [](TrackInfo const & t) { return t.year; }));
}

namespace
{

/// Encode a codepoint to UTF-8. Returns the byte sequence.
std::string encodeUtf8(char32_t ch)
{
    std::string out;
    if (ch < 0x80)
    {
        out.push_back(static_cast<char>(ch));
    }
    else if (ch < 0x800)
    {
        out.push_back(static_cast<char>(0xC0 | ((ch >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    }
    else if (ch < 0x10000)
    {
        out.push_back(static_cast<char>(0xE0 | ((ch >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | ((ch >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((ch >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    }
    return out;
}

/// Walk the byte offset one codepoint to the left. UTF-8 continuation
/// bytes have the top bits 10xxxxxx; stop at the first non-continuation.
int prevCpBoundary(std::string const & s, int pos)
{
    if (pos <= 0) return 0;
    --pos;
    while (pos > 0
           && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80)
    {
        --pos;
    }
    return pos;
}

/// Walk the byte offset one codepoint to the right.
int nextCpBoundary(std::string const & s, int pos)
{
    int const n = static_cast<int>(s.size());
    if (pos >= n) return n;
    ++pos;
    while (pos < n
           && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80)
    {
        ++pos;
    }
    return pos;
}

} // namespace

void TagEditDialog::insertUtf8(char32_t ch)
{
    if (_fields.empty()) return;
    Field & f = _fields[_focusedField];
    if (f.numeric)
    {
        // Digits only — silently drop anything else so users can hammer the
        // key without surprises.
        if (ch < '0' || ch > '9') return;
    }
    f.dirty = true;
    std::string const bytes = encodeUtf8(ch);
    f.cursorPos = std::clamp(f.cursorPos, 0, static_cast<int>(f.value.size()));
    f.value.insert(static_cast<std::size_t>(f.cursorPos), bytes);
    f.cursorPos += static_cast<int>(bytes.size());
}

void TagEditDialog::backspaceUtf8()
{
    if (_fields.empty()) return;
    Field & f = _fields[_focusedField];
    f.dirty = true;
    if (f.cursorPos <= 0) return;
    int const start = prevCpBoundary(f.value, f.cursorPos);
    f.value.erase(static_cast<std::size_t>(start),
                  static_cast<std::size_t>(f.cursorPos - start));
    f.cursorPos = start;
}

void TagEditDialog::deleteForward()
{
    if (_fields.empty()) return;
    Field & f = _fields[_focusedField];
    if (f.cursorPos >= static_cast<int>(f.value.size())) return;
    f.dirty = true;
    int const next = nextCpBoundary(f.value, f.cursorPos);
    f.value.erase(static_cast<std::size_t>(f.cursorPos),
                  static_cast<std::size_t>(next - f.cursorPos));
}

void TagEditDialog::moveCursorLeft()
{
    if (_fields.empty()) return;
    Field & f = _fields[_focusedField];
    f.cursorPos = prevCpBoundary(f.value, f.cursorPos);
}

void TagEditDialog::moveCursorRight()
{
    if (_fields.empty()) return;
    Field & f = _fields[_focusedField];
    f.cursorPos = nextCpBoundary(f.value, f.cursorPos);
}

void TagEditDialog::moveCursorHome()
{
    if (_fields.empty()) return;
    _fields[_focusedField].cursorPos = 0;
}

void TagEditDialog::moveCursorEnd()
{
    if (_fields.empty()) return;
    Field & f = _fields[_focusedField];
    f.cursorPos = static_cast<int>(f.value.size());
}

TagUpdate TagEditDialog::buildUpdate() const
{
    TagUpdate u;
    for (auto const & f : _fields)
    {
        if (!f.dirty) continue;
        std::string const k = f.key;
        if (k == "title")            u.title       = f.value;
        else if (k == "artist")      u.artist      = f.value;
        else if (k == "album")       u.album       = f.value;
        else if (k == "albumArtist") u.albumArtist = f.value;
        else if (k == "genre")       u.genre       = f.value;
        else if (k == "grouping")    u.grouping    = f.value;
        else if (k == "trackNumber") u.trackNumber = parseIntOrZero(f.value);
        else if (k == "discNumber")  u.discNumber  = parseIntOrZero(f.value);
        else if (k == "year")        u.year        = parseIntOrZero(f.value);
    }
    return u;
}

bool TagEditDialog::hasEdits() const
{
    for (auto const & f : _fields)
        if (f.dirty) return true;
    return false;
}

bool TagEditDialog::handleKey(ventty::KeyEvent const & event)
{
    if (!_open) return false;

    // ----- Confirm Save overlay -----
    if (_mode == Mode::ConfirmSave)
    {
        if (event.key == Key::Escape)
        {
            _mode = Mode::Edit;
            return true;
        }
        if (event.key == Key::Left || event.key == Key::Right
            || event.key == Key::Tab)
        {
            _confirmYes = !_confirmYes;
            return true;
        }
        if (event.key == Key::Char && !event.ctrl && !event.alt)
        {
            char32_t const ch = event.ch;
            if (ch == 'y' || ch == 'Y') { _confirmYes = true;  }
            else if (ch == 'n' || ch == 'N') { _confirmYes = false; }
        }
        if (event.key == Key::Enter)
        {
            if (_confirmYes)
            {
                TagUpdate const u = buildUpdate();
                if (_onSave && !u.empty())
                    _onSave(_targetPaths, u);
                close();
            }
            else
            {
                _mode = Mode::Edit;
            }
            return true;
        }
        return true; // swallow everything else
    }

    if (event.key == Key::Escape)
    {
        if (_mode == Mode::Edit)
        {
            // First ESC in Edit mode rolls the edits back and drops to View;
            // a second ESC (now in View) closes. _tracks is the untouched
            // snapshot from open(), so rebuilding fields restores originals.
            buildFields();
            _focusedField = 0;
            _mode = Mode::View;
            return true;
        }
        close();
        return true;
    }

    if (_fields.empty())
        return true;

    // ----- Mode toggles -----
    // F3 in View mode: enter Edit mode. Disabled when read-only — the
    // targets have no writable tags, so there is nothing to edit.
    if (_mode == Mode::View && !_readOnly && event.key == Key::F3)
    {
        _mode = Mode::Edit;
        return true;
    }
    // F2 in Edit mode: bring up the save-confirm overlay (only if the
    // user actually changed something — there's nothing to confirm otherwise).
    if (_mode == Mode::Edit && event.key == Key::F2)
    {
        if (hasEdits())
        {
            _mode = Mode::ConfirmSave;
            _confirmYes = true;
        }
        return true;
    }

    // ----- View mode swallows the rest (read-only) -----
    // Arrow keys/Tab are inert here: there is no field to move into when
    // nothing is editable, so they would only confuse the visual focus.
    if (_mode == Mode::View)
        return true;

    // ----- Edit mode: row movement -----
    if (event.key == Key::Up)
    {
        if (_focusedField > 0) --_focusedField;
        return true;
    }
    if (event.key == Key::Down || event.key == Key::Tab)
    {
        if (_focusedField < static_cast<int>(_fields.size()) - 1) ++_focusedField;
        return true;
    }

    // ----- Edit mode: text input -----
    if (event.key == Key::Left)
    {
        moveCursorLeft();
        return true;
    }
    if (event.key == Key::Right)
    {
        moveCursorRight();
        return true;
    }
    if (event.key == Key::Home)
    {
        moveCursorHome();
        return true;
    }
    if (event.key == Key::End)
    {
        moveCursorEnd();
        return true;
    }
    if (event.key == Key::Backspace)
    {
        backspaceUtf8();
        return true;
    }
    if (event.key == Key::Delete)
    {
        deleteForward();
        return true;
    }

    // Enter in Edit mode does nothing — saving goes through F2 → confirm.
    if (event.key == Key::Enter)
        return true;

    if (event.key == Key::Char && !event.ctrl && !event.alt && event.ch >= 0x20)
    {
        insertUtf8(event.ch);
        return true;
    }

    return true; // swallow other keys while modal
}

void TagEditDialog::draw(ventty::Window & window)
{
    _cursorScreenX = -1;
    _cursorScreenY = -1;

    if (!_open) return;

    int const screenW = window.width();
    int const screenH = window.height();

    int const fieldRows = static_cast<int>(_fields.size());
    int const desiredH  = 4 /*frame+header+sep+spacer*/ + fieldRows + 3 /*sep+footer+pad*/;
    int const dlgW = std::min(80, std::max(50, screenW - 8));
    int const dlgH = std::min(std::max(desiredH, 10), std::max(8, screenH - 4));
    int const x = (screenW - dlgW) / 2;
    int const y = (screenH - dlgH) / 2;

    drawEditor(window, x, y, dlgW, dlgH);

    if (_mode == Mode::ConfirmSave)
    {
        // Confirm overlay swallows the cursor — it has its own focus model.
        _cursorScreenX = -1;
        _cursorScreenY = -1;
        drawConfirm(window);
    }
}

void TagEditDialog::drawEditor(ventty::Window & window,
                               int x, int y, int dlgW, int dlgH)
{
    ventty::Style const frame {_theme.border,           _theme.background};
    ventty::Style const body  {_theme.browserFg,        _theme.browserBg};
    ventty::Style const accent{_theme.browserHeaderFg,  _theme.browserBg, ventty::Attr::Bold};
    ventty::Style const dim   {_theme.separatorFg,      _theme.browserBg};
    ventty::Style const sel   {_theme.browserSelFg,     _theme.browserSelBg};

    window.fill(x, y, dlgW, dlgH, U' ', body);
    window.drawBox(x, y, dlgW, dlgH, frame, /*doubleLine=*/true);

    // Title strip — title text reflects the current mode.
    std::string title;
    switch (_mode)
    {
    case Mode::View:        title = _readOnly ? " Tags (read-only) " : " Tags "; break;
    case Mode::Edit:        title = " Edit Tags ";  break;
    case Mode::ConfirmSave: title = " Edit Tags ";  break;
    }
    window.drawText(x + 2, y, title, accent);

    // Sub-header: scope description (the caller-provided label).
    std::string sub = "  " + _header;
    sub = truncateToWidth(sub, dlgW - 4, "...");
    window.drawText(x + 2, y + 1, sub, body);

    int const sepY = y + 2;
    for (int i = 1; i < dlgW - 1; ++i)
    {
        window.putChar(x + i, sepY, ventty::HR_THIN, frame);
    }

    // Determine label column width so values line up.
    int labelW = 0;
    for (auto const & f : _fields)
    {
        labelW = std::max(labelW, static_cast<int>(f.label.size()));
    }
    int const fieldX  = x + 4;
    int const valueX  = fieldX + labelW + 1;
    int const valueW  = (x + dlgW - 2) - valueX;

    bool const editable = (_mode == Mode::Edit);

    int const firstRowY = sepY + 2;
    int const fieldRows = static_cast<int>(_fields.size());
    for (int i = 0; i < fieldRows; ++i)
    {
        int const ry = firstRowY + i;
        if (ry >= y + dlgH - 2) break;

        bool const focused = (i == _focusedField);
        Field const & f = _fields[i];

        ventty::Style const labelStyle = focused ? accent : body;
        window.drawText(fieldX, ry, f.label, labelStyle);

        // Value cell — in Edit mode the focused row gets the selection fill
        // to mark the active edit target; in View mode we leave the row plain
        // so the dialog reads as a static info panel.
        ventty::Style const cellStyle =
            (focused && editable) ? sel : body;
        window.fill(valueX, ry, valueW, 1, U' ', cellStyle);

        bool showingPlaceholder = false;
        std::string display;
        int cursorCol = -1;

        if (f.value.empty() && !f.placeholder.empty() && !f.dirty)
        {
            display = truncateToWidth(f.placeholder, valueW, "");
            showingPlaceholder = true;
            if (focused && editable) cursorCol = valueX; // park cursor at the start
        }
        else if (!editable)
        {
            // Static view — just truncate. No horizontal scroll because
            // there's no cursor to anchor it on.
            display = truncateToWidth(f.value, valueW, "...");
        }
        else
        {
            // Edit mode: reserve one cell at the right edge so the cursor
            // has a place to sit when it follows the last character.
            // Compute a scroll offset (in display cells) that keeps the
            // cursor inside the visible window.
            int const reserve  = focused ? 1 : 0;
            int const showW    = std::max(0, valueW - reserve);

            std::string_view prefix(f.value.data(),
                                    static_cast<std::size_t>(std::clamp(
                                        f.cursorPos, 0,
                                        static_cast<int>(f.value.size()))));
            int const prefW = ventty::stringWidth(prefix);

            int scroll = 0;
            if (showW > 0 && prefW >= showW)
                scroll = prefW - (showW - 1);

            std::size_t pos = 0;
            int dropped = 0;
            while (pos < f.value.size() && dropped < scroll)
            {
                char32_t const cp = ventty::decode(f.value, pos);
                dropped += ventty::displayWidth(cp);
            }
            std::size_t const start = pos;
            int taken = 0;
            while (pos < f.value.size())
            {
                std::size_t const probe = pos;
                std::size_t p = pos;
                char32_t const cp = ventty::decode(f.value, p);
                int const w = ventty::displayWidth(cp);
                if (taken + w > showW) { pos = probe; break; }
                taken += w;
                pos = p;
            }
            display.assign(f.value, start, pos - start);

            if (focused)
            {
                cursorCol = valueX + (prefW - scroll);
                if (cursorCol < valueX)        cursorCol = valueX;
                if (cursorCol > valueX + valueW - 1)
                    cursorCol = valueX + valueW - 1;
            }
        }

        ventty::Style const textStyle = showingPlaceholder
                                            ? (focused && editable ? sel : dim)
                                            : cellStyle;
        window.drawText(valueX, ry, display, textStyle);

        if (focused && editable && cursorCol >= 0)
        {
            _cursorScreenX = cursorCol;
            _cursorScreenY = ry;
        }
    }

    // Footer separator.
    int const footerSepY = y + dlgH - 2;
    for (int i = 1; i < dlgW - 1; ++i)
    {
        window.putChar(x + i, footerSepY, ventty::HR_THIN, frame);
    }

    std::string footer;
    switch (_mode)
    {
    case Mode::View:
        footer = _readOnly ? " Read-only (plugin format)   ESC: close "
                           : " F3: edit   ESC: close ";
        break;
    case Mode::Edit:
        footer = " Tab/Up/Down: move   F2: save   ESC: revert ";
        break;
    case Mode::ConfirmSave:
        footer = " ←/→: choose   Enter: confirm   ESC: cancel ";
        break;
    }
    std::string footerCut = truncateToWidth(footer, dlgW - 4, "...");
    window.drawText(x + 2, y + dlgH - 1, footerCut, dim);
}

void TagEditDialog::drawConfirm(ventty::Window & window)
{
    int const screenW = window.width();
    int const screenH = window.height();

    int const boxW = 40;
    int const boxH = 7;
    int const bx = (screenW - boxW) / 2;
    int const by = (screenH - boxH) / 2;

    ventty::Style const frame {_theme.border,          _theme.background};
    ventty::Style const body  {_theme.browserFg,       _theme.browserBg};
    ventty::Style const accent{_theme.browserHeaderFg, _theme.browserBg, ventty::Attr::Bold};
    ventty::Style const sel   {_theme.browserSelFg,    _theme.browserSelBg};
    ventty::Style const dim   {_theme.separatorFg,     _theme.browserBg};

    window.fill(bx, by, boxW, boxH, U' ', body);
    window.drawBox(bx, by, boxW, boxH, frame, /*doubleLine=*/true);

    std::string const title = " Save changes? ";
    window.drawText(bx + 2, by, title, accent);

    std::string const prompt = "Write tag changes to the selected files?";
    std::string const promptCut = truncateToWidth(prompt, boxW - 4, "...");
    int const promptX = bx + (boxW - static_cast<int>(promptCut.size())) / 2;
    window.drawText(promptX, by + 2, promptCut, body);

    std::string const yesLabel = "  Yes  ";
    std::string const noLabel  = "  No  ";
    int const btnRowY = by + 4;
    int const yesX = bx + boxW / 2 - 8;
    int const noX  = bx + boxW / 2 + 2;

    ventty::Style const yesStyle = _confirmYes ? sel : body;
    ventty::Style const noStyle  = !_confirmYes ? sel : body;
    window.drawText(yesX, btnRowY, yesLabel, yesStyle);
    window.drawText(noX,  btnRowY, noLabel,  noStyle);

    (void)dim;
}

} // namespace vtplayer
