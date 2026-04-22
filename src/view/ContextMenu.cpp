// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "ContextMenu.h"

#include <ventty/core/Utf8.h>

namespace vtplayer
{

using Key = ventty::KeyEvent::Key;

void ContextMenu::setItems(std::vector<std::string> items)
{
    _items = std::move(items);
    if (_selected >= static_cast<int>(_items.size()))
    {
        _selected = 0;
    }
}

void ContextMenu::open()
{
    _open = true;
    _selected = 0;
}

void ContextMenu::close()
{
    _open = false;
}

bool ContextMenu::handleKey(ventty::KeyEvent const & event)
{
    if (!_open) return false;

    if (event.key == Key::Escape)
    {
        close();
        return true;
    }

    int const count = static_cast<int>(_items.size());
    if (count == 0) return true;

    if (event.key == Key::Up)
    {
        _selected = (_selected - 1 + count) % count;
        return true;
    }
    if (event.key == Key::Down)
    {
        _selected = (_selected + 1) % count;
        return true;
    }
    if (event.key == Key::Home)
    {
        _selected = 0;
        return true;
    }
    if (event.key == Key::End)
    {
        _selected = count - 1;
        return true;
    }
    if (event.key == Key::Enter)
    {
        int idx = _selected;
        close();
        if (_onSelect) _onSelect(idx);
        return true;
    }

    // Swallow all other input while open — this is a modal overlay.
    return true;
}

void ContextMenu::draw(ventty::Window & window)
{
    if (!_open) return;

    // Size: fit widest item + title, plus padding.
    int const titleW = static_cast<int>(ventty::stringWidth(_title));
    int itemsW = 0;
    for (auto const & item : _items)
    {
        int w = static_cast<int>(ventty::stringWidth(item));
        if (w > itemsW) itemsW = w;
    }

    static constexpr int kHPad = 4;  // " > item " → 2 each side
    static constexpr int kVPad = 1;  // blank row above and below items

    int innerW = std::max(titleW + 2, itemsW + kHPad);
    int boxW = innerW + 2;  // left/right borders
    int boxH = static_cast<int>(_items.size()) + 2 + 2 * kVPad;  // borders + pad + items

    int maxW = window.width() - 2;
    int maxH = window.height() - 2;
    if (boxW > maxW) boxW = maxW;
    if (boxH > maxH) boxH = maxH;

    int x = (window.width() - boxW) / 2;
    int y = (window.height() - boxH) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    ventty::Style bgStyle{_theme.playlistFg, _theme.playlistBg};
    ventty::Style borderStyle{_theme.border, _theme.playlistBg};
    ventty::Style titleStyle{_theme.headerTitleFg, _theme.playlistBg};
    ventty::Style itemStyle{_theme.playlistFg, _theme.playlistBg};
    ventty::Style selStyle{_theme.playlistSelFg, _theme.playlistSelBg};

    // Opaque background
    window.fill(x, y, boxW, boxH, U' ', bgStyle);

    // Border
    window.drawBox(x, y, boxW, boxH, borderStyle, /*doubleLine=*/true);

    // Title centered in top border
    if (!_title.empty() && boxW > titleW + 4)
    {
        std::string padded = " " + _title + " ";
        int tx = x + (boxW - static_cast<int>(ventty::stringWidth(padded))) / 2;
        window.drawText(tx, y, padded, titleStyle);
    }

    // Items
    int itemY = y + 1 + kVPad;
    int itemX = x + 1;
    int itemW = boxW - 2;
    for (size_t i = 0; i < _items.size(); ++i)
    {
        bool selected = (static_cast<int>(i) == _selected);
        ventty::Style const & style = selected ? selStyle : itemStyle;

        window.fill(itemX, itemY, itemW, 1, U' ', style);

        std::string prefix = selected ? " > " : "   ";
        window.drawText(itemX, itemY, prefix, style);

        std::string label = _items[i];
        int maxLabel = itemW - static_cast<int>(prefix.size()) - 1;
        if (maxLabel > 0 && static_cast<int>(ventty::stringWidth(label)) > maxLabel)
        {
            // Truncate with ellipsis; cheap byte-wise truncation is fine for ASCII.
            if (maxLabel > 2)
            {
                label = label.substr(0, maxLabel - 2) + "..";
            }
            else
            {
                label = label.substr(0, maxLabel);
            }
        }
        window.drawText(itemX + static_cast<int>(prefix.size()), itemY, label, style);
        ++itemY;
    }
}

} // namespace vtplayer
