// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "FileRenameDialog.h"

#include "../util/UnicodeNormalize.h"

#include <ventty/art/AsciiArt.h>
#include <ventty/core/Utf8.h>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace vtplayer
{

namespace
{

using Key = ventty::KeyEvent::Key;

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

int prevCpBoundary(std::string const & s, int pos)
{
    if (pos <= 0) return 0;
    --pos;
    while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80)
        --pos;
    return pos;
}

int nextCpBoundary(std::string const & s, int pos)
{
    int const n = static_cast<int>(s.size());
    if (pos >= n) return n;
    ++pos;
    while (pos < n && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80)
        ++pos;
    return pos;
}

std::string pad2(int v)
{
    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << v;
    return oss.str();
}

std::string sanitizeFilePart(std::string s)
{
    for (char & ch : s)
    {
        unsigned char const u = static_cast<unsigned char>(ch);
        if (u < 0x20 || ch == '/' || ch == ':' || ch == '\\' || ch == '*'
            || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|')
        {
            ch = '_';
        }
    }
    return s;
}

std::string trimmed(std::string const & s)
{
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

} // namespace

void FileRenameDialog::open(TrackInfo track)
{
    _open = true;
    _track = std::move(track);
    _name = _track.path.filename().string();
    _error.clear();
    _focus = 0;
    _cursorBytePos = static_cast<int>(_name.size());
    _cursorScreenX = -1;
    _cursorScreenY = -1;
    buildSuggestions();
}

void FileRenameDialog::close()
{
    _open = false;
    _track = {};
    _name.clear();
    _error.clear();
    _suggestions.clear();
    _suggestionEnabled.clear();
    _cursorScreenX = -1;
    _cursorScreenY = -1;
}

void FileRenameDialog::buildSuggestions()
{
    _suggestions.clear();
    _suggestionEnabled.clear();

    std::string const ext = _track.path.extension().string();
    std::string const title = sanitizeFilePart(_track.title);
    std::string const artist = sanitizeFilePart(_track.artist);

    auto add = [&](char const * label, bool enabled, std::string name)
    {
        _suggestions.emplace_back(label, enabled ? std::move(name) : std::string{});
        _suggestionEnabled.push_back(enabled);
    };

    add("Type 1:", _track.trackNumber > 0 && !_track.title.empty(),
        pad2(_track.trackNumber) + " " + title + ext);
    add("Type 2:", _track.discNumber > 0 && _track.trackNumber > 0 && !_track.title.empty(),
        std::to_string(_track.discNumber) + "." + pad2(_track.trackNumber) + " " + title + ext);
    add("Type 3:", !_track.artist.empty() && !_track.title.empty(),
        artist + " - " + title + ext);
    add("Type 4:", _track.trackNumber > 0 && !_track.artist.empty() && !_track.title.empty(),
        pad2(_track.trackNumber) + " " + artist + " - " + title + ext);
}

bool FileRenameDialog::suggestionEnabled(int idx) const
{
    int const opt = idx - 1;
    return opt >= 0
           && opt < static_cast<int>(_suggestionEnabled.size())
           && _suggestionEnabled[static_cast<std::size_t>(opt)];
}

void FileRenameDialog::moveFocus(int direction)
{
    int const count = 1 + static_cast<int>(_suggestions.size());
    if (count <= 1) return;

    int next = _focus;
    for (int tries = 0; tries < count; ++tries)
    {
        next += direction < 0 ? -1 : 1;
        if (next < 0) next = count - 1;
        if (next >= count) next = 0;
        if (next == 0 || suggestionEnabled(next))
        {
            _focus = next;
            return;
        }
    }
}

void FileRenameDialog::applySuggestion()
{
    if (!suggestionEnabled(_focus)) return;
    _name = _suggestions[static_cast<std::size_t>(_focus - 1)].second;
    _cursorBytePos = static_cast<int>(_name.size());
    _focus = 0;
    _error.clear();
}

void FileRenameDialog::save()
{
    std::string const value = trimmed(_name);
    if (value.empty())
    {
        _error = "Name cannot be empty";
        return;
    }
    if (_onConfirm)
    {
        std::optional<std::string> const err = _onConfirm(_track.path, value);
        if (err && !err->empty())
        {
            _error = *err;
            return;
        }
    }
    close();
}

void FileRenameDialog::insertUtf8(char32_t ch)
{
    std::string const bytes = encodeUtf8(ch);
    _cursorBytePos = std::clamp(_cursorBytePos, 0, static_cast<int>(_name.size()));
    _name.insert(static_cast<std::size_t>(_cursorBytePos), bytes);
    _cursorBytePos += static_cast<int>(bytes.size());
}

void FileRenameDialog::backspaceUtf8()
{
    if (_cursorBytePos <= 0) return;
    int const start = prevCpBoundary(_name, _cursorBytePos);
    _name.erase(static_cast<std::size_t>(start),
                static_cast<std::size_t>(_cursorBytePos - start));
    _cursorBytePos = start;
}

void FileRenameDialog::deleteForward()
{
    if (_cursorBytePos >= static_cast<int>(_name.size())) return;
    int const next = nextCpBoundary(_name, _cursorBytePos);
    _name.erase(static_cast<std::size_t>(_cursorBytePos),
                static_cast<std::size_t>(next - _cursorBytePos));
}

void FileRenameDialog::moveCursorLeft()
{
    _cursorBytePos = prevCpBoundary(_name, _cursorBytePos);
}

void FileRenameDialog::moveCursorRight()
{
    _cursorBytePos = nextCpBoundary(_name, _cursorBytePos);
}

bool FileRenameDialog::handleKey(ventty::KeyEvent const & event)
{
    if (!_open) return false;

    if (event.key == Key::Escape) { close(); return true; }
    if (event.key == Key::F2) { save(); return true; }
    if (event.key == Key::Tab || event.key == Key::Down) { moveFocus(+1); return true; }
    if (event.key == Key::Up) { moveFocus(-1); return true; }
    if (event.key == Key::Enter) { applySuggestion(); return true; }

    if (_focus != 0)
        return true;

    if (event.key == Key::Left) { moveCursorLeft(); return true; }
    if (event.key == Key::Right) { moveCursorRight(); return true; }
    if (event.key == Key::Home) { _cursorBytePos = 0; return true; }
    if (event.key == Key::End) { _cursorBytePos = static_cast<int>(_name.size()); return true; }
    if (event.key == Key::Backspace) { _error.clear(); backspaceUtf8(); return true; }
    if (event.key == Key::Delete) { _error.clear(); deleteForward(); return true; }

    if (event.key == Key::Char && !event.ctrl && !event.alt && event.ch >= 0x20)
    {
        _error.clear();
        insertUtf8(event.ch);
        return true;
    }

    return true;
}

void FileRenameDialog::draw(ventty::Window & window)
{
    _cursorScreenX = -1;
    _cursorScreenY = -1;
    if (!_open) return;

    int const screenW = window.width();
    int const screenH = window.height();
    int const dlgW = std::min(90, std::max(56, screenW - 8));
    int const dlgH = std::min(12, std::max(8, screenH - 4));
    int const x = (screenW - dlgW) / 2;
    int const y = (screenH - dlgH) / 2;

    ventty::Style const frame {_theme.border,          _theme.background};
    ventty::Style const body  {_theme.browserFg,       _theme.browserBg};
    ventty::Style const accent{_theme.browserHeaderFg, _theme.browserBg, ventty::Attr::Bold};
    ventty::Style const dim   {_theme.separatorFg,     _theme.browserBg};
    ventty::Style const sel   {_theme.browserSelFg,    _theme.browserSelBg};
    ventty::Style const errStyle{ventty::Color{0xCC, 0x6A, 0x6A}, _theme.browserBg};

    window.fill(x, y, dlgW, dlgH, U' ', body);
    window.drawBox(x, y, dlgW, dlgH, frame, /*doubleLine=*/true);
    window.drawText(x + 2, y, " Rename File ", accent);

    std::string current = " Current: " + _track.path.filename().string();
    current = truncateToWidth(current, dlgW - 4, "...");
    window.drawText(x + 2, y + 1, current, body);

    int const sepY = y + 2;
    for (int i = 1; i < dlgW - 1; ++i)
        window.putChar(x + i, sepY, ventty::HR_THIN, frame);

    int labelW = static_cast<int>(std::string("New name:").size());
    for (auto const & s : _suggestions)
        labelW = std::max(labelW, static_cast<int>(s.first.size()));

    int const labelX = x + 4;
    int const valueX = labelX + labelW + 1;
    int const valueW = std::max(1, (x + dlgW - 2) - valueX);
    int const firstY = y + 4;

    auto drawRow = [&](int row, std::string const & label, std::string const & value,
                       bool focused, bool enabled)
    {
        if (row >= y + dlgH - 2) return;
        ventty::Style const labelStyle = focused ? accent : (enabled ? body : dim);
        ventty::Style const valueStyle = focused ? sel : (enabled ? body : dim);
        window.drawText(labelX, row, label, labelStyle);
        window.fill(valueX, row, valueW, 1, U' ', valueStyle);
        window.drawText(valueX, row, truncateToWidth(value, valueW, "..."), valueStyle);
    };

    drawRow(firstY, "New name:", _name, _focus == 0, true);
    if (_focus == 0)
    {
        int const showW = std::max(0, valueW - 1);
        std::string_view prefix(_name.data(),
                                static_cast<std::size_t>(std::clamp(
                                    _cursorBytePos, 0, static_cast<int>(_name.size()))));
        int const prefW = ventty::stringWidth(prefix);
        int scroll = 0;
        if (showW > 0 && prefW >= showW)
            scroll = prefW - (showW - 1);

        std::size_t pos = 0;
        int dropped = 0;
        while (pos < _name.size() && dropped < scroll)
        {
            char32_t const cp = ventty::decode(_name, pos);
            dropped += ventty::displayWidth(cp);
        }
        std::size_t const start = pos;
        int taken = 0;
        while (pos < _name.size())
        {
            std::size_t probe = pos;
            std::size_t p = pos;
            char32_t const cp = ventty::decode(_name, p);
            int const w = ventty::displayWidth(cp);
            if (taken + w > showW) { pos = probe; break; }
            taken += w;
            pos = p;
        }
        std::string display(_name, start, pos - start);
        window.fill(valueX, firstY, valueW, 1, U' ', sel);
        window.drawText(valueX, firstY, display, sel);

        int cursorCol = valueX + (prefW - scroll);
        if (cursorCol < valueX) cursorCol = valueX;
        if (cursorCol > valueX + valueW - 1) cursorCol = valueX + valueW - 1;
        _cursorScreenX = cursorCol;
        _cursorScreenY = firstY;
    }

    for (int i = 0; i < static_cast<int>(_suggestions.size()); ++i)
    {
        bool const enabled = _suggestionEnabled[static_cast<std::size_t>(i)];
        drawRow(firstY + 1 + i,
                _suggestions[static_cast<std::size_t>(i)].first,
                enabled ? _suggestions[static_cast<std::size_t>(i)].second
                        : std::string("(unavailable)"),
                _focus == i + 1,
                enabled);
    }

    if (!_error.empty())
    {
        std::string msg = truncateToWidth(_error, dlgW - 4, "...");
        int const errY = y + dlgH - 3;
        if (errY > firstY + static_cast<int>(_suggestions.size()))
            window.drawText(x + 2, errY, msg, errStyle);
    }

    int const footerSepY = y + dlgH - 2;
    for (int i = 1; i < dlgW - 1; ++i)
        window.putChar(x + i, footerSepY, ventty::HR_THIN, frame);

    std::string footer = " Tab/Up/Down: choose   Enter: use suggestion   F2: rename   ESC: cancel ";
    window.drawText(x + 2, y + dlgH - 1,
                    truncateToWidth(footer, dlgW - 4, "..."), dim);
}

} // namespace vtplayer
