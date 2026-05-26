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

/// Trim codepoints off the LEFT until the string fits in `maxWidth` display
/// cells. Used to scroll long edit-buffers horizontally so the right end
/// (where the cursor lives) stays visible. Always cuts at codepoint
/// boundaries — never in the middle of a UTF-8 sequence.
std::string leftTruncateToWidth(std::string_view s, int maxWidth)
{
    if (maxWidth <= 0) return {};
    int total = ventty::stringWidth(s);
    if (total <= maxWidth) return std::string(s);
    std::size_t pos = 0;
    while (pos < s.size() && total > maxWidth)
    {
        char32_t const cp = ventty::decode(s, pos);
        total -= ventty::displayWidth(cp);
    }
    return std::string(s.substr(pos));
}

std::string intOrEmpty(int v)
{
    return v > 0 ? std::to_string(v) : std::string{};
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

void TagEditDialog::open(Scope scope,
                         std::string header,
                         std::vector<TrackInfo> tracks)
{
    _open = true;
    _scope = scope;
    _header = std::move(header);
    _tracks = std::move(tracks);
    _targetPaths.clear();
    _targetPaths.reserve(_tracks.size());
    for (auto const & t : _tracks)
        _targetPaths.push_back(t.path);

    buildFields();
    _focusedField = 0;
}

void TagEditDialog::close()
{
    _open = false;
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

    // For an Artist scope we present a single "Artist" field. Prefer the
    // common albumArtist (the library groups by it); if that's not uniform
    // fall back to artist. If both vary we still show the row but it
    // starts empty + flagged as <various>.
    if (_scope == Scope::Artist)
    {
        auto byAlbumArtist = commonString(_tracks,
            [](TrackInfo const & t) -> std::string const & { return t.albumArtist; });
        auto byArtist = commonString(_tracks,
            [](TrackInfo const & t) -> std::string const & { return t.artist; });
        std::optional<std::string> common;
        if (byAlbumArtist && !byAlbumArtist->empty())
            common = byAlbumArtist;
        else if (byArtist)
            common = byArtist;
        else
            common = byAlbumArtist; // both nullopt → still nullopt
        strField("Artist:", "artist", common);
        return;
    }

    bool const showTitleAndTrack = (_scope == Scope::SingleTrack
                                    || _scope == Scope::MultiTrack);

    if (showTitleAndTrack)
    {
        strField("Title:", "title",
                 commonString(_tracks, [](TrackInfo const & t) -> std::string const & { return t.title; }));
        intField("Track #:", "trackNumber",
                 commonInt(_tracks, [](TrackInfo const & t) { return t.trackNumber; }));
    }
    strField("Artist:", "artist",
             commonString(_tracks, [](TrackInfo const & t) -> std::string const & { return t.artist; }));
    strField("Album Artist:", "albumArtist",
             commonString(_tracks, [](TrackInfo const & t) -> std::string const & { return t.albumArtist; }));
    strField("Album:", "album",
             commonString(_tracks, [](TrackInfo const & t) -> std::string const & { return t.album; }));
    strField("Genre:", "genre",
             commonString(_tracks, [](TrackInfo const & t) -> std::string const & { return t.genre; }));
    strField("Grouping:", "grouping",
             commonString(_tracks, [](TrackInfo const & t) -> std::string const & { return t.grouping; }));
    intField("Disc:", "discNumber",
             commonInt(_tracks, [](TrackInfo const & t) { return t.discNumber; }));
    intField("Year:", "year",
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

void TagEditDialog::commit()
{
    // Saving is disabled in this release — the dialog is view-only.
    // Closing without invoking _onSave keeps the on-disk tags untouched.
    close();
}

bool TagEditDialog::handleKey(ventty::KeyEvent const & event)
{
    if (!_open) return false;

    if (event.key == Key::Escape)
    {
        close();
        return true;
    }

    if (_fields.empty())
        return true;

    // Row movement: Up/Down jump between fields, Tab too. Home/End take
    // the cursor to the start/end of the current value rather than
    // jumping fields — that matches what users expect in a text input.
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

    // Intra-line navigation.
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

    if (event.key == Key::Enter)
    {
        // No multi-line text to insert, so Enter commits the form
        // regardless of which field has focus.
        commit();
        return true;
    }

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

    // Width: roughly 70 cols, clamped to terminal. Height: header + per-
    // field row + footer + padding.
    int const fieldRows = static_cast<int>(_fields.size());
    int const desiredH  = 4 /*frame+header+sep+spacer*/ + fieldRows + 3 /*sep+footer+pad*/;
    int const dlgW = std::min(80, std::max(50, screenW - 8));
    int const dlgH = std::min(std::max(desiredH, 10), std::max(8, screenH - 4));
    int const x = (screenW - dlgW) / 2;
    int const y = (screenH - dlgH) / 2;

    ventty::Style const frame {_theme.border,           _theme.background};
    ventty::Style const body  {_theme.browserFg,        _theme.browserBg};
    ventty::Style const accent{_theme.browserHeaderFg,  _theme.browserBg, ventty::Attr::Bold};
    ventty::Style const dim   {_theme.separatorFg,      _theme.browserBg};
    ventty::Style const sel   {_theme.browserSelFg,     _theme.browserSelBg};

    window.fill(x, y, dlgW, dlgH, U' ', body);
    window.drawBox(x, y, dlgW, dlgH, frame, /*doubleLine=*/true);

    // Title strip.
    std::string title = " Edit Tags ";
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

    int const firstRowY = sepY + 2;
    for (int i = 0; i < fieldRows; ++i)
    {
        int const ry = firstRowY + i;
        if (ry >= y + dlgH - 2) break;

        bool const focused = (i == _focusedField);
        Field const & f = _fields[i];

        ventty::Style const labelStyle = focused ? accent : body;
        window.drawText(fieldX, ry, f.label, labelStyle);

        // Value cell — full-width fill so the focused row is obvious.
        ventty::Style const cellStyle = focused ? sel : body;
        window.fill(valueX, ry, valueW, 1, U' ', cellStyle);

        bool showingPlaceholder = false;
        std::string display;
        int cursorCol = -1;

        if (f.value.empty() && !f.placeholder.empty() && !f.dirty)
        {
            display = truncateToWidth(f.placeholder, valueW, "");
            showingPlaceholder = true;
            if (focused) cursorCol = valueX; // park cursor at the start
        }
        else
        {
            // Reserve one cell at the right edge so the cursor has a place
            // to sit when it follows the last character. Compute a scroll
            // offset (in display cells) that keeps the cursor inside the
            // visible window — once the prefix exceeds the budget, push
            // the visible window right so the cursor stays one cell from
            // the right edge.
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

            // Drop `scroll` cells from the front, then take up to `showW`
            // cells. Both walk codepoint boundaries — never byte slices.
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
                                            ? (focused ? sel : dim)
                                            : cellStyle;
        window.drawText(valueX, ry, display, textStyle);

        if (focused && cursorCol >= 0)
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

    std::string const footer = " Tab/Up/Down: move   ESC: close ";
    std::string footerCut = truncateToWidth(footer, dlgW - 4, "...");
    window.drawText(x + 2, y + dlgH - 1, footerCut, dim);
}

} // namespace vtplayer
