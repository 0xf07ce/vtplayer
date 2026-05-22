// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "LibrarySearchDialog.h"

#include "../library/MediaLibrary.h"
#include "../util/UnicodeNormalize.h"

#include <ventty/art/AsciiArt.h>
#include <ventty/core/Utf8.h>

#include <algorithm>
#include <cctype>

namespace vtplayer
{

namespace
{

using Key = ventty::KeyEvent::Key;

/// Trim codepoints off the LEFT until the string fits in `maxWidth`
/// display cells. Mirrors TagEditDialog's horizontal-scroll helper so the
/// cursor end of the query stays visible without splitting UTF-8 bytes.
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

/// Step `pos` to the start of the previous UTF-8 codepoint in `s`.
std::size_t prevCodepointStart(std::string const & s, std::size_t pos)
{
    if (pos == 0) return 0;
    --pos;
    while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80)
        --pos;
    return pos;
}

/// Step `pos` to the start of the next UTF-8 codepoint in `s`.
std::size_t nextCodepointStart(std::string const & s, std::size_t pos)
{
    if (pos >= s.size()) return s.size();
    std::size_t p = pos;
    ventty::decode(s, p);
    return p;
}

std::string toLowerAscii(std::string const & in)
{
    std::string out;
    out.reserve(in.size());
    for (char c : in)
    {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

bool containsCI(std::string const & haystackLower, std::string const & needleLower)
{
    if (needleLower.empty()) return true;
    return haystackLower.find(needleLower) != std::string::npos;
}

bool matches(TrackInfo const & t, std::string const & needleLower)
{
    if (needleLower.empty()) return true;
    if (containsCI(toLowerAscii(t.title),       needleLower)) return true;
    if (containsCI(toLowerAscii(t.artist),      needleLower)) return true;
    if (containsCI(toLowerAscii(t.album),       needleLower)) return true;
    if (containsCI(toLowerAscii(t.albumArtist), needleLower)) return true;
    if (containsCI(toLowerAscii(t.genre),       needleLower)) return true;
    return false;
}

std::string formatRow(TrackInfo const & t)
{
    std::string artist = !t.albumArtist.empty() ? t.albumArtist : t.artist;
    std::string row;
    if (!artist.empty()) { row += artist; row += " — "; }
    if (!t.album.empty()) { row += t.album; row += " / "; }
    row += t.title.empty() ? t.path.stem().string() : t.title;
    return row;
}

} // namespace

void LibrarySearchDialog::open()
{
    _open = true;
    _query.clear();
    _cursorBytePos = 0;
    _selectedIndex = 0;
    _scrollOffset = 0;
    _cursorScreenX = -1;
    _cursorScreenY = -1;
    recomputeMatches();
}

void LibrarySearchDialog::close()
{
    _open = false;
    _cursorScreenX = -1;
    _cursorScreenY = -1;
}

void LibrarySearchDialog::recomputeMatches()
{
    _matches.clear();
    if (!_library) return;
    std::string const needle = toLowerAscii(_query);
    for (auto const & t : _library->tracks())
    {
        if (matches(t, needle)) _matches.push_back(&t);
    }
    if (_selectedIndex >= static_cast<int>(_matches.size()))
    {
        _selectedIndex = std::max(0, static_cast<int>(_matches.size()) - 1);
    }
    _scrollOffset = 0;
}

void LibrarySearchDialog::insertUtf8(char32_t ch)
{
    char buf[4];
    int n = 0;
    if (ch < 0x80)
    {
        buf[n++] = static_cast<char>(ch);
    }
    else if (ch < 0x800)
    {
        buf[n++] = static_cast<char>(0xC0 | ((ch >> 6) & 0x1F));
        buf[n++] = static_cast<char>(0x80 | (ch & 0x3F));
    }
    else if (ch < 0x10000)
    {
        buf[n++] = static_cast<char>(0xE0 | ((ch >> 12) & 0x0F));
        buf[n++] = static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
        buf[n++] = static_cast<char>(0x80 | (ch & 0x3F));
    }
    else
    {
        buf[n++] = static_cast<char>(0xF0 | ((ch >> 18) & 0x07));
        buf[n++] = static_cast<char>(0x80 | ((ch >> 12) & 0x3F));
        buf[n++] = static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
        buf[n++] = static_cast<char>(0x80 | (ch & 0x3F));
    }
    _query.insert(_cursorBytePos, buf, n);
    _cursorBytePos += n;
}

void LibrarySearchDialog::backspaceUtf8()
{
    if (_cursorBytePos == 0) return;
    std::size_t const prev = prevCodepointStart(_query, _cursorBytePos);
    _query.erase(prev, _cursorBytePos - prev);
    _cursorBytePos = static_cast<int>(prev);
}

void LibrarySearchDialog::deleteForward()
{
    if (_cursorBytePos >= static_cast<int>(_query.size())) return;
    std::size_t const next = nextCodepointStart(_query, _cursorBytePos);
    _query.erase(_cursorBytePos, next - _cursorBytePos);
}

void LibrarySearchDialog::moveCursorLeft()
{
    if (_cursorBytePos == 0) return;
    _cursorBytePos = static_cast<int>(prevCodepointStart(_query, _cursorBytePos));
}

void LibrarySearchDialog::moveCursorRight()
{
    if (_cursorBytePos >= static_cast<int>(_query.size())) return;
    _cursorBytePos = static_cast<int>(nextCodepointStart(_query, _cursorBytePos));
}

void LibrarySearchDialog::moveCursorHome()
{
    _cursorBytePos = 0;
}

void LibrarySearchDialog::moveCursorEnd()
{
    _cursorBytePos = static_cast<int>(_query.size());
}

bool LibrarySearchDialog::handleKey(ventty::KeyEvent const & event)
{
    if (!_open) return false;

    if (event.key == Key::Escape)
    {
        close();
        return true;
    }

    if (event.key == Key::Backspace)
    {
        backspaceUtf8();
        recomputeMatches();
        return true;
    }

    if (event.key == Key::Delete)
    {
        deleteForward();
        recomputeMatches();
        return true;
    }

    // Cursor movement inside the query line. Home/End are shared with list
    // navigation, so we only repurpose them when the query has content —
    // an empty query leaves Home/End free to jump to first/last result.
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

    if (event.key == Key::Up)
    {
        if (_selectedIndex > 0) _selectedIndex--;
        return true;
    }
    if (event.key == Key::Down || event.key == Key::Tab)
    {
        if (_selectedIndex < static_cast<int>(_matches.size()) - 1) _selectedIndex++;
        return true;
    }
    if (event.key == Key::PageUp)
    {
        _selectedIndex = std::max(0, _selectedIndex - 8);
        return true;
    }
    if (event.key == Key::PageDown)
    {
        _selectedIndex = std::min(static_cast<int>(_matches.size()) - 1, _selectedIndex + 8);
        return true;
    }
    if (event.key == Key::Home)
    {
        if (!_query.empty()) moveCursorHome();
        else _selectedIndex = 0;
        return true;
    }
    if (event.key == Key::End)
    {
        if (!_query.empty()) moveCursorEnd();
        else if (!_matches.empty()) _selectedIndex = static_cast<int>(_matches.size()) - 1;
        return true;
    }

    if (event.key == Key::Enter)
    {
        if (!_matches.empty() && _onLocate)
        {
            _onLocate(_matches[_selectedIndex]->path);
        }
        close();
        return true;
    }

    if (event.key == Key::Char && !event.ctrl && !event.alt && event.ch >= 0x20)
    {
        insertUtf8(event.ch);
        recomputeMatches();
        return true;
    }
    return true; // swallow all other keys while modal
}

void LibrarySearchDialog::draw(ventty::Window & window)
{
    _cursorScreenX = -1;
    _cursorScreenY = -1;
    if (!_open) return;

    int const screenW = window.width();
    int const screenH = window.height();
    int const dlgW = std::min(80, std::max(40, screenW - 8));
    int const dlgH = std::min(20, std::max(8,  screenH - 6));
    int const x = (screenW - dlgW) / 2;
    int const y = (screenH - dlgH) / 2;

    ventty::Style const frame{_theme.border,            _theme.background};
    ventty::Style const body { _theme.browserFg,        _theme.browserBg};
    ventty::Style const accent{_theme.browserHeaderFg,  _theme.browserBg, ventty::Attr::Bold};
    ventty::Style const sel  { _theme.browserSelFg,     _theme.browserSelBg};

    // Frame
    window.fill(x, y, dlgW, dlgH, U' ', body);
    window.drawBox(x, y, dlgW, dlgH, frame, /*doubleLine=*/true);

    // Title
    std::string title = " Search Library ";
    int titleX = x + 2;
    window.drawText(titleX, y, title, accent);

    // Query line — cell-width aware so CJK aligns and the cursor lands on
    // the right cell. We slide the visible window leftwards just enough to
    // keep the cursor in view, then stash terminal-cell coords for the
    // host to drive ventty's hardware cursor.
    int const queryY = y + 2;
    std::string const label = " Query: ";
    window.drawText(x + 2, queryY, label, accent);
    int const queryX = x + 2 + static_cast<int>(ventty::stringWidth(label));
    int const queryW = dlgW - (queryX - x) - 2;

    int const prefixW = ventty::stringWidth(
        std::string_view{_query.data(), static_cast<std::size_t>(_cursorBytePos)});

    // Reserve one cell for the cursor itself when it sits at end-of-line.
    int const showW = std::max(1, queryW - 1);
    std::string visiblePrefix =
        leftTruncateToWidth(std::string_view{_query.data(),
                                             static_cast<std::size_t>(_cursorBytePos)},
                            showW);
    int const droppedW = prefixW - ventty::stringWidth(visiblePrefix);
    std::string const tail =
        _cursorBytePos < static_cast<int>(_query.size())
            ? _query.substr(_cursorBytePos)
            : std::string{};
    std::string visibleQuery = visiblePrefix;
    int const remaining = showW - ventty::stringWidth(visiblePrefix);
    if (remaining > 0 && !tail.empty())
    {
        // Append codepoints from tail until we exhaust visible cells.
        std::size_t pos = 0;
        int taken = 0;
        while (pos < tail.size())
        {
            std::size_t probe = pos;
            char32_t const cp = ventty::decode(tail, probe);
            int const w = ventty::displayWidth(cp);
            if (taken + w > remaining) break;
            taken += w;
            pos = probe;
        }
        visibleQuery.append(tail, 0, pos);
    }
    window.drawText(queryX, queryY, visibleQuery, body);

    int const cursorCol = queryX + (prefixW - droppedW);
    if (cursorCol >= queryX && cursorCol < x + dlgW - 1)
    {
        _cursorScreenX = cursorCol;
        _cursorScreenY = queryY;
    }

    // Separator
    int const sepY = y + 3;
    for (int i = 1; i < dlgW - 1; ++i)
    {
        window.putChar(x + i, sepY, ventty::HR_THIN, frame);
    }

    // Result count
    std::string countLine = "  " + std::to_string(_matches.size()) + " match"
                            + (_matches.size() == 1 ? "" : "es");
    window.drawText(x + 2, y + 1, countLine, body);

    // Result list (virtual scroll)
    int const listY     = sepY + 1;
    int const listH     = dlgH - (listY - y) - 1;
    int const innerW    = dlgW - 4;

    // Adjust scroll so the selection stays visible.
    if (_selectedIndex < _scrollOffset) _scrollOffset = _selectedIndex;
    if (_selectedIndex >= _scrollOffset + listH)
    {
        _scrollOffset = _selectedIndex - listH + 1;
    }
    if (_scrollOffset < 0) _scrollOffset = 0;

    for (int i = 0; i < listH; ++i)
    {
        int const idx = _scrollOffset + i;
        int const ry  = listY + i;
        if (idx >= static_cast<int>(_matches.size())) break;

        bool const cursor = (idx == _selectedIndex);
        ventty::Style const rowStyle = cursor ? sel : body;
        window.fill(x + 1, ry, dlgW - 2, 1, U' ', rowStyle);

        std::string text = formatRow(*_matches[idx]);
        text = truncateToWidth(text, innerW, "...");
        window.drawText(x + 2, ry, text, rowStyle);
    }
}

} // namespace vtplayer
