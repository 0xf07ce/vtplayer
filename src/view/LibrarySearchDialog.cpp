// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "LibrarySearchDialog.h"

#include "../library/MediaLibrary.h"
#include "../util/UnicodeNormalize.h"

#include <ventty/art/AsciiArt.h>
#include <ventty/core/Utf8.h>

#include <algorithm>
#include <array>
#include <cctype>

namespace vtplayer
{

namespace
{

using Key = ventty::KeyEvent::Key;
using Filter = LibrarySearchDialog::Filter;

constexpr std::array<std::pair<Filter, char const *>, 5> kTabs = {{
    {Filter::Any,    "Any"},
    {Filter::Artist, "Artist"},
    {Filter::Album,  "Album"},
    {Filter::Title,  "Title"},
    {Filter::Year,   "Year"},
}};

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

std::string toLowerAscii(std::string_view in)
{
    std::string out;
    out.reserve(in.size());
    for (char c : in)
    {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
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
    // Rebuild on every open so an intervening library reload (scan
    // completion, tag edit, root change) can never serve stale rows.
    // The active filter (`_filter`) survives across opens as a UI
    // preference; only the query and result cursor are reset.
    rebuildHaystack();
    recomputeMatches();
}

void LibrarySearchDialog::rebuildHaystack()
{
    _haystack.clear();
    if (!_library) return;
    auto const & tracks = _library->tracks();
    _haystack.reserve(tracks.size());
    for (auto const & t : tracks)
    {
        HaystackRow row;
        row.track  = &t;
        // Artist field folds in albumArtist too — a single hit on either
        // should count, since the displayed name on compilations is the
        // albumArtist.
        std::string artistBlob = t.artist;
        if (!t.albumArtist.empty())
        {
            if (!artistBlob.empty()) artistBlob.push_back('\t');
            artistBlob += t.albumArtist;
        }
        row.artist = toLowerAscii(artistBlob);
        row.album  = toLowerAscii(t.album);
        row.title  = toLowerAscii(t.title);
        row.year   = (t.year > 0) ? std::to_string(t.year) : std::string{};
        // `any` blob — tabs are a sentinel that can't appear in a typed
        // needle, so a substring hit anywhere here means at least one of
        // the included fields matches.
        std::string anyBlob = t.title;
        anyBlob.push_back('\t'); anyBlob += t.artist;
        anyBlob.push_back('\t'); anyBlob += t.album;
        anyBlob.push_back('\t'); anyBlob += t.albumArtist;
        anyBlob.push_back('\t'); anyBlob += t.genre;
        if (t.year > 0)
        {
            anyBlob.push_back('\t');
            anyBlob += std::to_string(t.year);
        }
        row.any = toLowerAscii(anyBlob);
        _haystack.push_back(std::move(row));
    }
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
    std::string const needle = toLowerAscii(_query);
    auto pickField = [this](HaystackRow const & r) -> std::string const & {
        switch (_filter)
        {
            case Filter::Artist: return r.artist;
            case Filter::Album:  return r.album;
            case Filter::Title:  return r.title;
            case Filter::Year:   return r.year;
            case Filter::Any:
            default:             return r.any;
        }
    };
    if (needle.empty())
    {
        _matches.reserve(_haystack.size());
        for (auto const & row : _haystack) _matches.push_back(row.track);
    }
    else
    {
        for (auto const & row : _haystack)
        {
            if (pickField(row).find(needle) != std::string::npos)
                _matches.push_back(row.track);
        }
    }
    if (_selectedIndex >= static_cast<int>(_matches.size()))
    {
        _selectedIndex = std::max(0, static_cast<int>(_matches.size()) - 1);
    }
    _scrollOffset = 0;
}

void LibrarySearchDialog::cycleFilter(int dir)
{
    int const n = static_cast<int>(kTabs.size());
    int idx = 0;
    for (int i = 0; i < n; ++i)
    {
        if (kTabs[i].first == _filter) { idx = i; break; }
    }
    idx = ((idx + dir) % n + n) % n;
    _filter = kTabs[idx].first;
    _selectedIndex = 0;
    _scrollOffset  = 0;
    recomputeMatches();
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

    // Tab / Shift+Tab cycle the filter tab bar. Down/Up own list navigation
    // now — Tab is no longer aliased to Down.
    if (event.key == Key::Tab)
    {
        cycleFilter(event.shift ? -1 : +1);
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
    if (event.key == Key::Down)
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
        if (!_matches.empty())
        {
            // Snapshot paths for n / N navigation outside the dialog. The
            // pointer array would dangle on a library rebuild, so we keep
            // owned paths and re-resolve via LibraryView::locate.
            _navPaths.clear();
            _navPaths.reserve(_matches.size());
            for (auto const * t : _matches) _navPaths.push_back(t->path);
            _navIndex = _selectedIndex;
            if (_onLocate)
            {
                _onLocate(_matches[_selectedIndex]->path);
            }
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

bool LibrarySearchDialog::navigateNext()
{
    if (_navPaths.empty() || !_onLocate) return false;
    int const n = static_cast<int>(_navPaths.size());
    _navIndex = (_navIndex + 1) % n;
    _onLocate(_navPaths[_navIndex]);
    return true;
}

bool LibrarySearchDialog::navigatePrev()
{
    if (_navPaths.empty() || !_onLocate) return false;
    int const n = static_cast<int>(_navPaths.size());
    _navIndex = (_navIndex - 1 + n) % n;
    _onLocate(_navPaths[_navIndex]);
    return true;
}

void LibrarySearchDialog::invalidateNav()
{
    _navPaths.clear();
    _navIndex = -1;
}

void LibrarySearchDialog::draw(ventty::Window & window)
{
    _cursorScreenX = -1;
    _cursorScreenY = -1;
    if (!_open) return;

    int const screenW = window.width();
    int const screenH = window.height();
    int const dlgW = std::min(80, std::max(48, screenW - 8));
    int const dlgH = std::min(20, std::max(9,  screenH - 6));
    int const x = (screenW - dlgW) / 2;
    int const y = (screenH - dlgH) / 2;

    ventty::Style const frame{_theme.border,            _theme.background};
    ventty::Style const body { _theme.browserFg,        _theme.browserBg};
    ventty::Style const dim  { _theme.headerFg,         _theme.browserBg};
    ventty::Style const accent{_theme.browserHeaderFg,  _theme.browserBg, ventty::Attr::Bold};
    ventty::Style const tabSel{_theme.browserSelFg,     _theme.browserSelBg, ventty::Attr::Bold};
    ventty::Style const sel  { _theme.browserSelFg,     _theme.browserSelBg};

    // Frame
    window.fill(x, y, dlgW, dlgH, U' ', body);
    window.drawBox(x, y, dlgW, dlgH, frame, /*doubleLine=*/true);

    // Title (top border overlay)
    std::string title = " Search Library ";
    int titleX = x + 2;
    window.drawText(titleX, y, title, accent);

    // Tab bar (y+1): filter scope on the left, result count on the right.
    int const tabY = y + 1;
    int tabX = x + 2;
    for (auto const & [f, label] : kTabs)
    {
        bool const active = (_filter == f);
        std::string item = " ";
        item += label;
        item += " ";
        window.drawText(tabX, tabY, item, active ? tabSel : dim);
        tabX += static_cast<int>(ventty::stringWidth(item)) + 1;
    }
    std::string countLine = std::to_string(_matches.size()) + " match"
                            + (_matches.size() == 1 ? "" : "es");
    int const countW = ventty::stringWidth(countLine);
    int const countX = x + dlgW - 2 - countW;
    if (countX > tabX)
    {
        window.drawText(countX, tabY, countLine, dim);
    }

    // Query line (y+2) — cell-width aware so CJK aligns and the cursor lands
    // on the right cell. We slide the visible window leftwards just enough
    // to keep the cursor in view, then stash terminal-cell coords for the
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
