// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "VisualizerView.h"

#include <ventty/art/AsciiArt.h>

namespace vtamp
{

    void VisualizerView::setVisualizer(std::unique_ptr<Visualizer> vis)
    {
        _vis = std::move(vis);
        if (_vis)
            _vis->setTheme(_theme);
    }

    void VisualizerView::update(AudioEngine const &engine)
    {
        if (_vis)
            _vis->update(engine);
    }

    void VisualizerView::draw(ventty::Window &window)
    {
        auto const &r = rect();

        // Left + right borders
        ventty::Style borderStyle{_theme.border, _theme.background};
        for (int y = 0; y < r.height; ++y)
        {
            window.putChar(r.x, r.y + y, ventty::DOUBLE_BOX.v, borderStyle);
            window.putChar(r.x + r.width - 1, r.y + y, ventty::DOUBLE_BOX.v, borderStyle);
        }

        // Content area (inside borders)
        int cx = r.x + 2;
        int cw = r.width - 4;
        int cy = r.y;

        // Track info box at bottom (3 rows)
        int boxH = 3;
        int visH = r.height - boxH;
        if (visH < 2)
            visH = 2;

        // Visualizer fills main area
        if (_vis)
        {
            _vis->draw(window, cx, cy, cw, visH);
        }

        // Track info box below the visualizer
        drawTrackBox(window, cx, cy + visH, cw);
    }

    void VisualizerView::drawTrackBox(ventty::Window &window, int x, int y, int w)
    {
        ventty::Style boxStyle{_theme.border, _theme.background};
        ventty::Style textStyle{_theme.foreground, _theme.background};
        ventty::Style labelStyle{_theme.visLabelFg, _theme.background};

        // Draw box border (3 rows)
        window.putChar(x, y, ventty::ROUND_BOX.tl, boxStyle);
        window.putChar(x, y + 1, ventty::ROUND_BOX.v, boxStyle);
        window.putChar(x, y + 2, ventty::ROUND_BOX.bl, boxStyle);
        window.putChar(x + w - 1, y, ventty::ROUND_BOX.tr, boxStyle);
        window.putChar(x + w - 1, y + 1, ventty::ROUND_BOX.v, boxStyle);
        window.putChar(x + w - 1, y + 2, ventty::ROUND_BOX.br, boxStyle);

        for (int i = x + 1; i < x + w - 1; ++i)
        {
            window.putChar(i, y, ventty::ROUND_BOX.h, boxStyle);
            window.putChar(i, y + 2, ventty::ROUND_BOX.h, boxStyle);
        }

        // Fill middle row
        window.fill(x + 1, y + 1, w - 2, 1, U' ', textStyle);

        // Track name
        if (!_trackName.empty())
        {
            std::string display = " " + _trackName;
            if (!_trackInfo.empty())
            {
                display += "  " + _trackInfo;
            }
            display += " ";

            int textX = x + 2;
            window.drawText(textX, y + 1, display, textStyle);
        }
        else
        {
            window.drawText(x + 2, y + 1, " No track loaded ", labelStyle);
        }
    }

} // namespace vtamp
