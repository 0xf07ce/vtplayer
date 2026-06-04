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

std::string PlaylistsView::selectedName() const
{
    if (_names.empty()) return {};
    if (_selectedIndex < 0 || _selectedIndex >= static_cast<int>(_names.size()))
        return {};
    return _names[_selectedIndex];
}

void PlaylistsView::moveCursor(int delta)
{
    if (_names.empty()) return;
    int const n = static_cast<int>(_names.size());
    _selectedIndex = std::clamp(_selectedIndex + delta, 0, n - 1);
}

void PlaylistsView::ensureVisible(int listH)
{
    if (listH <= 0) return;
    if (_selectedIndex < _scrollOffset) _scrollOffset = _selectedIndex;
    if (_selectedIndex >= _scrollOffset + listH)
        _scrollOffset = _selectedIndex - listH + 1;
    if (_scrollOffset < 0) _scrollOffset = 0;
}

bool PlaylistsView::handleKey(ventty::KeyEvent const & event)
{
    if (event.key == Key::Up) { moveCursor(-1); return true; }
    if (event.key == Key::Down) { moveCursor(+1); return true; }
    if (event.key == Key::PageUp) { moveCursor(-8); return true; }
    if (event.key == Key::PageDown) { moveCursor(+8); return true; }
    if (event.key == Key::Home) { _selectedIndex = 0; return true; }
    if (event.key == Key::End)
    {
        _selectedIndex = std::max(0, static_cast<int>(_names.size()) - 1);
        return true;
    }
    if (event.key == Key::Enter)
    {
        // TODO: load the selected playlist's tracks into the play queue.
        // Intentionally a no-op for now; the OnActivate seam stays unwired.
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

    // Header
    ventty::Style headerTextStyle{_theme.browserHeaderFg, _theme.browserBg, ventty::Attr::Bold};
    window.fill(r.x + 1, r.y, r.width - 1, 1, U' ', headerTextStyle);
    std::string header = truncateToWidth(" Playlists", r.width - 2, "...");
    window.drawText(r.x + 1, r.y, header, headerTextStyle);

    // Separator line
    ventty::Style sepStyle{_theme.border, _theme.browserBg};
    for (int x = r.x + 1; x < r.x + r.width; ++x)
    {
        window.putChar(x, r.y + 1, ventty::HR_THIN, sepStyle);
    }

    int const listH = r.height - 2;
    int const contentW = r.width - 2;

    // Empty-state hint.
    if (_names.empty())
    {
        ventty::Style hintStyle{_theme.separatorFg, _theme.browserBg};
        std::string hint = truncateToWidth("No playlists — press ESC to create one",
                                           contentW, "...");
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

} // namespace vtplayer
