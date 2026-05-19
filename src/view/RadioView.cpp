// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "RadioView.h"

#include "../util/UnicodeNormalize.h"

#include <ventty/art/AsciiArt.h>
#include <ventty/core/Utf8.h>
#include <ventty/core/Window.h>

#include <algorithm>

namespace vtplayer
{

using Key = ventty::KeyEvent::Key;

void RadioView::setStreams(std::vector<Stream> streams)
{
    _streams = std::move(streams);
    _selectedIndex = 0;
    _scrollOffset = 0;
}

void RadioView::scrollToSelected()
{
    int const listH = std::max(1, rect().height - 2);
    if (_selectedIndex < _scrollOffset)
        _scrollOffset = _selectedIndex;
    else if (_selectedIndex >= _scrollOffset + listH)
        _scrollOffset = _selectedIndex - listH + 1;
    int const maxOff =
        std::max(0, static_cast<int>(_streams.size()) - listH);
    _scrollOffset = std::clamp(_scrollOffset, 0, maxOff);
}

void RadioView::draw(ventty::Window & window)
{
    auto const & r = rect();
    if (r.width <= 0 || r.height <= 0)
        return;

    // Left border (matches FileBrowser / LibraryView framing).
    for (int y = 0; y < r.height; ++y)
        window.putChar(r.x, r.y + y, ventty::DOUBLE_BOX.v,
                       ventty::Style{_theme.border, _theme.browserBg});

    // Header
    ventty::Style headerStyle{_theme.browserHeaderFg, _theme.browserBg,
                              ventty::Attr::Bold};
    window.fill(r.x + 1, r.y, r.width - 1, 1, U' ', headerStyle);
    std::string header =
        " Radio (" + std::to_string(_streams.size()) + ")";
    header = truncateToWidth(header, r.width - 2, "...");
    window.drawText(r.x + 1, r.y, header, headerStyle);

    ventty::Style sepStyle{_theme.border, _theme.browserBg};
    for (int x = r.x + 1; x < r.x + r.width; ++x)
        window.putChar(x, r.y + 1, ventty::HR_THIN, sepStyle);

    int const listH = r.height - 2;
    int const contentW = r.width - 2;

    for (int i = 0; i < listH; ++i)
    {
        int const idx = _scrollOffset + i;
        int const y = r.y + 2 + i;

        if (idx >= static_cast<int>(_streams.size()))
        {
            window.fill(r.x + 1, y, r.width - 1, 1, U' ',
                        ventty::Style{_theme.browserFg, _theme.browserBg});
            continue;
        }

        bool const cursor  = (idx == _selectedIndex) && isFocused();
        bool const playing = (idx == _playingIndex);

        Color bg = cursor ? _theme.browserSelBg : _theme.browserBg;
        Color fg = cursor    ? _theme.browserSelFg
                   : playing ? _theme.browserAudioFg
                             : _theme.browserFg;

        ventty::Style style{fg, bg};
        window.fill(r.x + 1, y, r.width - 1, 1, U' ', style);

        std::string icon = playing ? " \xE2\x96\xB6 "    // ▶ now playing
                                    : " \xE2\x99\xAB ";  // ♫
        window.drawText(r.x + 1, y, icon, style);
        int const textX =
            r.x + 1 + static_cast<int>(ventty::stringWidth(icon));

        std::string name =
            truncateToWidth(_streams[idx].name, contentW - 4);
        window.drawText(textX, y, name, style);
    }
}

bool RadioView::handleKey(ventty::KeyEvent const & event)
{
    if (_streams.empty())
        return false;

    int const last = static_cast<int>(_streams.size()) - 1;

    if (event.key == Key::Up ||
        (event.key == Key::Char && event.ch == 'k'))
    {
        if (_selectedIndex > 0)
            --_selectedIndex;
        scrollToSelected();
        return true;
    }
    if (event.key == Key::Down ||
        (event.key == Key::Char && event.ch == 'j'))
    {
        if (_selectedIndex < last)
            ++_selectedIndex;
        scrollToSelected();
        return true;
    }
    if (event.key == Key::Home)
    {
        _selectedIndex = 0;
        scrollToSelected();
        return true;
    }
    if (event.key == Key::End)
    {
        _selectedIndex = last;
        scrollToSelected();
        return true;
    }
    if (event.key == Key::PageUp)
    {
        _selectedIndex = std::max(0, _selectedIndex - std::max(1, rect().height - 2));
        scrollToSelected();
        return true;
    }
    if (event.key == Key::PageDown)
    {
        _selectedIndex = std::min(last, _selectedIndex + std::max(1, rect().height - 2));
        scrollToSelected();
        return true;
    }
    if (event.key == Key::Enter)
    {
        if (_selectedIndex >= 0 && _selectedIndex <= last && _onPlay)
        {
            _playingIndex = _selectedIndex;
            _onPlay(_streams[_selectedIndex]);
        }
        return true;
    }
    return false;
}

bool RadioView::handleMouse(ventty::MouseEvent const & event)
{
    using Action = ventty::MouseEvent::Action;
    using Button = ventty::MouseEvent::Button;
    auto const & r = rect();
    if (!r.contains(event.x, event.y))
        return false;

    if (event.button == Button::ScrollUp)
    {
        if (_selectedIndex > 0) --_selectedIndex;
        scrollToSelected();
        return true;
    }
    if (event.button == Button::ScrollDown)
    {
        if (_selectedIndex < static_cast<int>(_streams.size()) - 1)
            ++_selectedIndex;
        scrollToSelected();
        return true;
    }

    if (event.button == Button::Left &&
        (event.action == Action::Press || event.action == Action::Release))
    {
        int const idx = _scrollOffset + (event.y - (r.y + 2));
        if (idx >= 0 && idx < static_cast<int>(_streams.size()))
        {
            _selectedIndex = idx;
            scrollToSelected();
            if (event.action == Action::Press && _onPlay)
            {
                _playingIndex = idx;
                _onPlay(_streams[idx]);
            }
            return true;
        }
    }
    return false;
}

} // namespace vtplayer
