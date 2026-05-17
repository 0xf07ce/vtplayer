// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "HeaderBar.h"

#include <ventty/art/AsciiArt.h>

#ifndef VTPLAYER_VERSION
#define VTPLAYER_VERSION "unknown"
#endif

namespace vtplayer
{

void HeaderBar::draw(ventty::Window & window)
{
    auto const & r = rect();
    ventty::Style borderStyle{_theme.border, _theme.headerBg};
    ventty::Style titleStyle{_theme.headerTitleFg, _theme.headerBg, ventty::Attr::Bold};
    ventty::Style dimStyle{_theme.headerFg, _theme.headerBg};

    // Fill background
    window.fill(r.x, r.y, r.width, 1, U' ', dimStyle);

    // Draw double-line top border with title
    window.putChar(r.x, r.y, ventty::DOUBLE_BOX.tl, borderStyle);

    int x = r.x + 1;
    window.putChar(x++, r.y, ventty::DOUBLE_BOX.h, borderStyle);
    window.putChar(x++, r.y, U'[', borderStyle);

    std::string title = " VT-PLAYER " VTPLAYER_VERSION " ";
    window.drawText(x, r.y, title, titleStyle);
    x += static_cast<int>(title.size());

    window.putChar(x++, r.y, U']', borderStyle);

    // Fill rest with double horizontal line
    for (; x < r.x + r.width - 1; ++x)
    {
        window.putChar(x, r.y, ventty::DOUBLE_BOX.h, borderStyle);
    }
    window.putChar(r.x + r.width - 1, r.y, ventty::DOUBLE_BOX.tr, borderStyle);
}

} // namespace vtplayer
