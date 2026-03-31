// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "HeaderBar.h"

#include <ventty/art/AsciiArt.h>

namespace vtplayer
{

void HeaderBar::draw(ventty::Window & window)
{
    auto const & r = rect();
    ventty::Style borderStyle{_theme.border, _theme.headerBg};
    ventty::Style titleStyle{_theme.headerTitleFg, _theme.headerBg, ventty::Attr::Bold};
    ventty::Style trackStyle{_theme.headerTrackFg, _theme.headerBg};
    ventty::Style dimStyle{_theme.headerFg, _theme.headerBg};

    // Fill background
    window.fill(r.x, r.y, r.width, 1, U' ', dimStyle);

    // Draw double-line top border with title
    window.putChar(r.x, r.y, ventty::DOUBLE_BOX.tl, borderStyle);

    int x = r.x + 1;
    window.putChar(x++, r.y, ventty::DOUBLE_BOX.h, borderStyle);
    window.putChar(x++, r.y, U'[', borderStyle);

    std::string title = " VT-PLAYER ";
    window.drawText(x, r.y, title, titleStyle);
    x += static_cast<int>(title.size());

    window.putChar(x++, r.y, U']', borderStyle);
    window.putChar(x++, r.y, ventty::DOUBLE_BOX.h, borderStyle);

    if (_playing && !_trackName.empty())
    {
        window.putChar(x++, r.y, ventty::DOUBLE_BOX.h, borderStyle);
        std::string nowPlaying = " Now: " + _trackName + " ";
        window.drawText(x, r.y, nowPlaying, trackStyle);
        x += static_cast<int>(nowPlaying.size());

        if (!_trackTime.empty())
        {
            std::string timeStr = _trackTime + " ";
            window.drawText(x, r.y, timeStr, dimStyle);
            x += static_cast<int>(timeStr.size());
        }
    }

    // Fill rest with double horizontal line
    for (; x < r.x + r.width - 1; ++x)
    {
        window.putChar(x, r.y, ventty::DOUBLE_BOX.h, borderStyle);
    }
    window.putChar(r.x + r.width - 1, r.y, ventty::DOUBLE_BOX.tr, borderStyle);
}

} // namespace vtplayer
