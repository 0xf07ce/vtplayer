// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "LibrarySearchDialog.h"

#include "../library/MediaLibrary.h"
#include "../util/UnicodeNormalize.h"

#include <ventty/art/AsciiArt.h>

#include <algorithm>
#include <cctype>

namespace vtplayer
{

namespace
{

using Key = ventty::KeyEvent::Key;

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
    _selectedIndex = 0;
    _scrollOffset = 0;
    recomputeMatches();
}

void LibrarySearchDialog::close()
{
    _open = false;
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

void LibrarySearchDialog::appendUtf8(char32_t ch)
{
    // Encode to UTF-8.
    if (ch < 0x80)
    {
        _query.push_back(static_cast<char>(ch));
    }
    else if (ch < 0x800)
    {
        _query.push_back(static_cast<char>(0xC0 | ((ch >> 6) & 0x1F)));
        _query.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    }
    else if (ch < 0x10000)
    {
        _query.push_back(static_cast<char>(0xE0 | ((ch >> 12) & 0x0F)));
        _query.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
        _query.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    }
    else
    {
        _query.push_back(static_cast<char>(0xF0 | ((ch >> 18) & 0x07)));
        _query.push_back(static_cast<char>(0x80 | ((ch >> 12) & 0x3F)));
        _query.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
        _query.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    }
}

void LibrarySearchDialog::backspaceUtf8()
{
    if (_query.empty()) return;
    // Walk back to the start of the previous codepoint (continuation bytes
    // have the high bits 10xxxxxx).
    while (!_query.empty())
    {
        unsigned char b = static_cast<unsigned char>(_query.back());
        _query.pop_back();
        if ((b & 0xC0) != 0x80) break;
    }
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
        _selectedIndex = 0;
        return true;
    }
    if (event.key == Key::End)
    {
        if (!_matches.empty()) _selectedIndex = static_cast<int>(_matches.size()) - 1;
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
        appendUtf8(event.ch);
        recomputeMatches();
        return true;
    }
    return true; // swallow all other keys while modal
}

void LibrarySearchDialog::draw(ventty::Window & window)
{
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

    // Query line
    int const queryY = y + 2;
    std::string label = " Query: ";
    window.drawText(x + 2, queryY, label, accent);
    int const queryX = x + 2 + static_cast<int>(label.size());
    int const queryW = dlgW - (queryX - x) - 2;
    std::string visibleQuery = _query;
    if (static_cast<int>(visibleQuery.size()) > queryW - 1)
    {
        visibleQuery = visibleQuery.substr(visibleQuery.size() - (queryW - 1));
    }
    window.drawText(queryX, queryY, visibleQuery, body);
    // Cursor block at the end of the query.
    int const cursorX = queryX + static_cast<int>(visibleQuery.size());
    if (cursorX < x + dlgW - 1)
    {
        window.putChar(cursorX, queryY, U'_', accent);
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
