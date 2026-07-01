// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "Oscilloscope.h"

#include <ventty/art/BrailleCanvas.h>

#include <algorithm>

namespace vtplayer
{

    void Oscilloscope::update(AudioEngine const &engine)
    {
        engine.getSamples(_samples.data(), static_cast<int>(_samples.size()));
    }

    void Oscilloscope::draw(ventty::Window &window, int x, int y, int w, int h)
    {
        if (w <= 0 || h <= 0)
            return;

        int const N = static_cast<int>(_samples.size());
        // Braille sub-pixels are tiny; the light theme needs near-black
        // contrast so the dotted trace doesn't disappear on the pale bg.
        ventty::Color const kTrace =
            _theme.isLight ? ventty::Color{0x14, 0x10, 0x1F}
                           : ventty::Color{0xC9, 0xA0, 0x62};
        ventty::Style traceStyle{kTrace, _theme.background};

        // Plot the trace into a Braille sub-pixel canvas (2x4 dots per cell):
        // one sample per sub-column, consecutive points joined so the wave
        // is continuous (a point plot leaves vertical gaps — and a sparse
        // centre — wherever adjacent samples jump between extremes).
        ventty::BrailleCanvas canvas(w, h);
        int const subW = canvas.subWidth();
        int const subH = canvas.subHeight();
        int const denom = std::max(1, subW - 1);
        // Unity gain matches the post-libav sample stream: above 1.0 the
        // louder rails clip into the clamp too often.
        constexpr float kGain = 1.0f;
        auto subY = [&](int sx) {
            int idx = (sx * (N - 1)) / denom;
            float s = std::clamp(_samples[idx] * kGain, -1.0f, 1.0f);
            // s = +1 → top sub-row, s = -1 → bottom sub-row.
            return static_cast<int>((1.0f - (s + 1.0f) * 0.5f) * (subH - 1)
                                    + 0.5f);
        };
        int prevY = subY(0);
        canvas.set(0, prevY);
        for (int sx = 1; sx < subW; ++sx)
        {
            int const y0 = subY(sx);
            canvas.line(sx - 1, prevY, sx, y0);
            prevY = y0;
        }

        canvas.blit(window, x, y, traceStyle);
    }

} // namespace vtplayer
