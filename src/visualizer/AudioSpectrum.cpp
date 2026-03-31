// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "AudioSpectrum.h"

#include <ventty/art/AsciiArt.h>

#include <algorithm>
#include <cmath>
#include <complex>

namespace vtamp
{

    void AudioSpectrum::update(AudioEngine const &engine)
    {
        float samples[512];
        engine.getSamples(samples, 512);

        // Apply Hann window
        for (int i = 0; i < 512; ++i)
        {
            float t = static_cast<float>(i) / 511.0f;
            float hann = 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * t));
            samples[i] *= hann;
        }

        // FFT
        std::complex<float> fftOut[512];
        _fft.forward(samples, fftOut);

        float mag[256];
        _fft.magnitude(fftOut, mag);

        // Group into logarithmic frequency bands
        for (int i = 0; i < NUM_BARS; ++i)
        {
            float frac0 = static_cast<float>(i) / static_cast<float>(NUM_BARS);
            float frac1 = static_cast<float>(i + 1) / static_cast<float>(NUM_BARS);

            int bin0 = static_cast<int>(std::pow(256.0f, frac0));
            int bin1 = static_cast<int>(std::pow(256.0f, frac1));
            bin0 = std::clamp(bin0, 1, 255);
            bin1 = std::clamp(bin1, bin0 + 1, 256);

            float sum = 0.0f;
            for (int b = bin0; b < bin1; ++b)
            {
                sum += mag[b];
            }
            float avg = sum / static_cast<float>(bin1 - bin0);

            // Normalize (log scale)
            float db = 20.0f * std::log10(avg + 1e-10f);
            float normalized = (db + 60.0f) / 60.0f;
            normalized = std::clamp(normalized, 0.0f, 1.0f);

            // Smooth with decay
            _barValues[i] = std::max(normalized, _barValues[i] * 0.82f);
            _peakValues[i] = std::max(_barValues[i], _peakValues[i] * 0.96f);
        }
    }

    void AudioSpectrum::draw(ventty::Window &window, int x, int y, int w, int h)
    {
        int totalBars = std::min(NUM_BARS, w);
        int barW = w / totalBars;
        if (barW < 1)
            barW = 1;
        totalBars = w / barW;

        for (int i = 0; i < totalBars && i < NUM_BARS; ++i)
        {
            int bx = x + i * barW;
            float val = _barValues[i];

            // Map value to height in 1/8 increments
            float heightF = val * static_cast<float>(h) * 8.0f;
            int fullRows = static_cast<int>(heightF) / 8;
            int partial = static_cast<int>(heightF) % 8;

            for (int row = 0; row < h; ++row)
            {
                int drawY = y + h - 1 - row;
                char32_t ch = U' ';
                Color fg = _theme.visBarLow;

                // Color gradient: low → mid → high
                float rowFrac = static_cast<float>(row) / static_cast<float>(h);
                if (rowFrac > 0.75f)
                    fg = _theme.visBarHigh;
                else if (rowFrac > 0.45f)
                    fg = _theme.visBarMid;

                if (row < fullRows)
                {
                    ch = ventty::VBAR[8]; // full block
                }
                else if (row == fullRows && partial > 0)
                {
                    ch = ventty::VBAR[partial];
                }

                ventty::Style style{fg, _theme.background};
                for (int bw = 0; bw < barW && bx + bw < x + w; ++bw)
                {
                    window.putChar(bx + bw, drawY, ch, style);
                }
            }
        }
    }

} // namespace vtamp
