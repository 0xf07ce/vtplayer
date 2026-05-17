// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "VinylVis.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vtplayer
{
    namespace
    {
        constexpr double kSpinPerFrame = 0.05;  ///< radians/frame (~60fps)
        constexpr double kGrooveRings = 7.0;    ///< faint concentric bands
        constexpr int kPaletteSteps = 24;       ///< body color quantization

        Color lerp(Color a, Color b, double t)
        {
            t = std::clamp(t, 0.0, 1.0);
            auto mix = [&](uint8_t x, uint8_t y)
            {
                return static_cast<uint8_t>(std::lround(x + (y - x) * t));
            };
            return Color(mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b));
        }
    } // namespace

    void VinylVis::update(AudioEngine const & /*engine*/)
    {
        // Frame-driven spin. (Tie speed to playback / spectrum later.)
        _angle += kSpinPerFrame;
        if (_angle >= 2.0 * M_PI)
            _angle -= 2.0 * M_PI;
    }

    void VinylVis::draw(ventty::Window &window, int x, int y, int w, int h)
    {
        if (w <= 0 || h <= 0)
            return;

        // Braille cells give a 2x4 sub-pixel grid per character. A terminal
        // cell is ~1:2 (W:H), so 2 sub-cols x 4 sub-rows yields square
        // sub-pixels — the disc comes out round without aspect fudging.
        int const subW = w * 2;
        int const subH = h * 4;
        double const scx = subW / 2.0;
        double const scy = subH / 2.0;
        double const R = std::min(subW, subH) / 2.0 - 1.0;
        if (R < 2.0)
            return;

        double const labelR = std::max(3.0, R * 0.30);
        double const holeR = std::max(1.5, R * 0.06);
        double const grooveFreq = kGrooveRings * 2.0 * M_PI / std::max(1.0, R - labelR);

        static unsigned char const kDot[4][2] = {
            {0x01, 0x08}, {0x02, 0x10}, {0x04, 0x20}, {0x40, 0x80},
        };

        // Body palette: small spread (subtle grooves + gentle rotating
        // sheen) between two close theme shades — no harsh contrast.
        std::vector<ventty::Style> palette;
        palette.reserve(kPaletteSteps);
        for (int i = 0; i < kPaletteSteps; ++i)
        {
            double t = static_cast<double>(i) / (kPaletteSteps - 1);
            // Top end pushed a bit past visBarMid toward visBarHigh so the
            // rotating sheen reads more clearly (still a gentle spread).
            Color hi = lerp(_theme.visBarMid, _theme.visBarHigh, 0.65);
            palette.push_back({lerp(_theme.visBarLow, hi, t),
                               _theme.background});
        }
        ventty::Style const labelStyle{_theme.visBarLow, _theme.background};

        for (int cy = 0; cy < h; ++cy)
            for (int cx = 0; cx < w; ++cx)
            {
                // Fill dots per sub-pixel for a crisp circular edge.
                unsigned char m = 0;
                for (int sr = 0; sr < 4; ++sr)
                    for (int sc = 0; sc < 2; ++sc)
                    {
                        double dx = (cx * 2 + sc) - scx;
                        double dy = (cy * 4 + sr) - scy;
                        double rr = std::sqrt(dx * dx + dy * dy);
                        if (rr <= R && rr >= holeR)
                            m |= kDot[sr][sc];
                    }
                if (!m)
                    continue;

                // One color per cell, sampled at the cell center — the
                // gradient is smooth so per-cell is plenty.
                double dx = (cx * 2 + 0.5) - scx;
                double dy = (cy * 4 + 1.5) - scy;
                double rr = std::sqrt(dx * dx + dy * dy);

                ventty::Style st;
                if (rr <= labelR)
                {
                    st = labelStyle;
                }
                else
                {
                    double phi = std::atan2(dy, dx);
                    // Rotating sheen: a single bright lobe sweeping with
                    // _angle makes the whole face read as spinning.
                    double sheen = 0.5 + 0.5 * std::cos(phi - _angle);
                    // Faint concentric grooves (small amplitude).
                    double groove = 0.5 + 0.5 * std::sin((rr - labelR) * grooveFreq);
                    double v = 0.14 + 0.78 * sheen + 0.10 * groove;
                    int idx = static_cast<int>(std::lround(
                        std::clamp(v, 0.0, 1.0) * (kPaletteSteps - 1)));
                    st = palette[idx];
                }

                window.putChar(x + cx, y + cy,
                               static_cast<char32_t>(0x2800u + m), st);
            }
    }

} // namespace vtplayer
