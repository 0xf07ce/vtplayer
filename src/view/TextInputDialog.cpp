// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "TextInputDialog.h"

#include "../util/UnicodeNormalize.h"

#include <ventty/core/Utf8.h>

#include <algorithm>
#include <cctype>

namespace vtplayer
{

namespace
{

using Key = ventty::KeyEvent::Key;

std::size_t prevCodepointStart(std::string const & s, std::size_t pos)
{
    if (pos == 0) return 0;
    --pos;
    while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80)
        --pos;
    return pos;
}

std::size_t nextCodepointStart(std::string const & s, std::size_t pos)
{
    if (pos >= s.size()) return s.size();
    std::size_t p = pos;
    ventty::decode(s, p);
    return p;
}

/// Trim codepoints off the LEFT until the string fits in `maxWidth` cells,
/// keeping the cursor end of the input visible without splitting UTF-8 bytes.
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

std::string trimmed(std::string const & s)
{
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

} // namespace

void TextInputDialog::open(std::string title, std::string prompt, std::string initial)
{
    _open = true;
    _title = std::move(title);
    _prompt = std::move(prompt);
    _text = std::move(initial);
    _error.clear();
    _cursorBytePos = static_cast<int>(_text.size()); // cursor at end of prefill
    _cursorScreenX = -1;
    _cursorScreenY = -1;
}

void TextInputDialog::close()
{
    _open = false;
    _cursorScreenX = -1;
    _cursorScreenY = -1;
}

void TextInputDialog::insertUtf8(char32_t ch)
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
    _text.insert(_cursorBytePos, buf, n);
    _cursorBytePos += n;
}

void TextInputDialog::backspaceUtf8()
{
    if (_cursorBytePos == 0) return;
    std::size_t const prev = prevCodepointStart(_text, _cursorBytePos);
    _text.erase(prev, _cursorBytePos - prev);
    _cursorBytePos = static_cast<int>(prev);
}

void TextInputDialog::deleteForward()
{
    if (_cursorBytePos >= static_cast<int>(_text.size())) return;
    std::size_t const next = nextCodepointStart(_text, _cursorBytePos);
    _text.erase(_cursorBytePos, next - _cursorBytePos);
}

void TextInputDialog::moveCursorLeft()
{
    if (_cursorBytePos == 0) return;
    _cursorBytePos = static_cast<int>(prevCodepointStart(_text, _cursorBytePos));
}

void TextInputDialog::moveCursorRight()
{
    if (_cursorBytePos >= static_cast<int>(_text.size())) return;
    _cursorBytePos = static_cast<int>(nextCodepointStart(_text, _cursorBytePos));
}

bool TextInputDialog::handleKey(ventty::KeyEvent const & event)
{
    if (!_open) return false;

    if (event.key == Key::Escape) { close(); return true; }
    if (event.key == Key::Backspace) { _error.clear(); backspaceUtf8(); return true; }
    if (event.key == Key::Delete) { _error.clear(); deleteForward(); return true; }
    if (event.key == Key::Left) { moveCursorLeft(); return true; }
    if (event.key == Key::Right) { moveCursorRight(); return true; }
    if (event.key == Key::Home) { _cursorBytePos = 0; return true; }
    if (event.key == Key::End) { _cursorBytePos = static_cast<int>(_text.size()); return true; }

    if (event.key == Key::Enter)
    {
        std::string const value = trimmed(_text);
        if (value.empty())
        {
            _error = "Name cannot be empty";
            return true;
        }
        // The callback validates (e.g. name collision) and returns an error to
        // keep the dialog open, or nullopt to accept and close.
        if (_onConfirm)
        {
            std::optional<std::string> const err = _onConfirm(value);
            if (err && !err->empty())
            {
                _error = *err;
                return true;
            }
        }
        close();
        return true;
    }

    if (event.key == Key::Char && !event.ctrl && !event.alt && event.ch >= 0x20)
    {
        _error.clear();
        insertUtf8(event.ch);
        return true;
    }
    return true; // swallow all other keys while modal
}

void TextInputDialog::draw(ventty::Window & window)
{
    _cursorScreenX = -1;
    _cursorScreenY = -1;
    if (!_open) return;

    int const screenW = window.width();
    int const screenH = window.height();
    int const dlgW = std::min(60, std::max(36, screenW - 8));
    int const dlgH = 6; // border + field row + error row + padding
    int const x = (screenW - dlgW) / 2;
    int const y = (screenH - dlgH) / 2;

    ventty::Style const frame { _theme.border,          _theme.background};
    ventty::Style const body  { _theme.browserFg,       _theme.browserBg};
    ventty::Style const accent{ _theme.browserHeaderFg, _theme.browserBg, ventty::Attr::Bold};

    window.fill(x, y, dlgW, dlgH, U' ', body);
    window.drawBox(x, y, dlgW, dlgH, frame, /*doubleLine=*/true);

    std::string const title = " " + _title + " ";
    window.drawText(x + 2, y, title, accent);

    // Prompt label + input field on the same row.
    int const fieldY = y + 2;
    std::string const label = " " + _prompt + " ";
    window.drawText(x + 2, fieldY, label, accent);
    int const fieldX = x + 2 + static_cast<int>(ventty::stringWidth(label));
    int const fieldW = dlgW - (fieldX - x) - 2;

    int const prefixW = ventty::stringWidth(
        std::string_view{_text.data(), static_cast<std::size_t>(_cursorBytePos)});

    int const showW = std::max(1, fieldW - 1); // reserve a cell for the cursor
    std::string visiblePrefix =
        leftTruncateToWidth(std::string_view{_text.data(),
                                             static_cast<std::size_t>(_cursorBytePos)},
                            showW);
    int const droppedW = prefixW - ventty::stringWidth(visiblePrefix);
    std::string const tail =
        _cursorBytePos < static_cast<int>(_text.size())
            ? _text.substr(_cursorBytePos)
            : std::string{};
    std::string visibleText = visiblePrefix;
    int const remaining = showW - ventty::stringWidth(visiblePrefix);
    if (remaining > 0 && !tail.empty())
    {
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
        visibleText.append(tail, 0, pos);
    }
    window.drawText(fieldX, fieldY, visibleText, body);

    int const cursorCol = fieldX + (prefixW - droppedW);
    if (cursorCol >= fieldX && cursorCol < x + dlgW - 1)
    {
        _cursorScreenX = cursorCol;
        _cursorScreenY = fieldY;
    }

    // Validation hint (e.g. name collision) below the field, in a warning
    // tint. Cleared as soon as the user edits the text.
    if (!_error.empty())
    {
        ventty::Style const errStyle{ventty::Color{0xCC, 0x6A, 0x6A}, _theme.browserBg};
        std::string const msg = truncateToWidth(_error, dlgW - 4, "...");
        window.drawText(x + 2, fieldY + 1, msg, errStyle);
    }
}

} // namespace vtplayer
