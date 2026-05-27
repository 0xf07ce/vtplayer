// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "AudioSpectrum.h"

#include <ventty/art/AsciiArt.h>

#include <algorithm>
#include <cmath>
#include <complex>

namespace vtplayer
{

    AudioSpectrum::AudioSpectrum(int numBars)
    {
        int n = std::clamp(numBars, 4, 256);
        _barValues.assign(n, 0.0f);
        _peakValues.assign(n, 0.0f);
    }

    void AudioSpectrum::update(AudioEngine const &engine)
    {
        int const numBars = static_cast<int>(_barValues.size());

        // When not actively playing, the audio thread isn't writing to the
        // visualization ring buffer. getSamples would just hand back the
        // last samples it saw, keeping the FFT (and the bars) lit on stale
        // data — that's why bars used to hover at ~50% long after a track
        // ended. Decay quickly toward zero instead.
        if (engine.state() != PlayState::Playing)
        {
            for (auto &v : _barValues) v *= 0.6f;
            return;
        }

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

        // Group into logarithmic frequency bands. The final band sits right
        // up against Nyquist (≈19.6–22 kHz at 44.1 kHz Fs) where MP3's low-
        // pass leaves nothing — that bar always ended up conspicuously low.
        // Skip it; draw() also drops it so the remaining bars take the full
        // width.
        for (int i = 0; i < numBars - 1; ++i)
        {
            float frac0 = static_cast<float>(i) / static_cast<float>(numBars);
            float frac1 = static_cast<float>(i + 1) / static_cast<float>(numBars);

            int bin0 = static_cast<int>(std::pow(256.0f, frac0));
            int bin1 = static_cast<int>(std::pow(256.0f, frac1));
            bin0 = std::clamp(bin0, 1, 255);
            bin1 = std::clamp(bin1, bin0 + 1, 256);

            float sum = 0.0f;
            for (int b = bin0; b < bin1; ++b)
            {
                sum += mag[b];
            }

            // Sum (not average) so log-spaced bars at the high end aren't
            // penalized for spanning many FFT bins.
            float db = 20.0f * std::log10(sum + 1e-10f);

            // Per-band linear-in-dB tilt that compensates music's natural
            // high-frequency rolloff (and MP3's low-pass). 0 dB at the
            // lowest band, +18 dB at the top — enough to lift the right
            // side off the floor without amplifying the noise floor when
            // the band is genuinely quiet.
            float tiltDb = (numBars > 1)
                               ? (static_cast<float>(i) / static_cast<float>(numBars - 1)) * 18.0f
                               : 0.0f;
            db += tiltDb;

            // Floor at -35 dB, headroom up to +35 dB. Tighter than before so
            // truly quiet bands collapse to zero instead of hovering at the
            // tilt-amplified noise level.
            float normalized = (db + 35.0f) / 70.0f;
            normalized = std::clamp(normalized, 0.0f, 1.0f);

            // Smooth with decay
            _barValues[i] = std::max(normalized, _barValues[i] * 0.82f);
            _peakValues[i] = std::max(_barValues[i], _peakValues[i] * 0.96f);
        }
        // Keep the dropped Nyquist-edge bar pinned to silence so the trail
        // buffer can decay it cleanly even after switching tracks.
        if (numBars > 0)
        {
            _barValues[numBars - 1] = 0.0f;
            _peakValues[numBars - 1] = 0.0f;
        }
    }

    void AudioSpectrum::draw(ventty::Window &window, int x, int y, int w, int h)
    {
        int const numBars = static_cast<int>(_barValues.size());
        if (w <= 0 || h <= 0 || numBars <= 0)
            return;
        // Spread numBars evenly across the full width using floor((i*w)/N)
        // boundaries. With plain integer division (barW = w / N) the right
        // (w - barW*N) columns end up perpetually blank — that's why the
        // right slice of the spectrum looked empty regardless of audio.
        // We also drop the very last band (see update()'s `numBars - 1`
        // loop) since it sits in the Nyquist dead zone.
        int const drawBars = numBars - 1;
        if (drawBars <= 0)
            return;
        int totalBars = std::min(drawBars, w);

        Color const bg = _theme.background;

        // Pre-compute the row-wise gradient: bottom row = visBarLow (deep
        // purple), top row = visBarHigh (light purple), with visBarMid
        // anchoring the midpoint. All bars share the same column of colors;
        // only their height differs.
        std::vector<Color> rowColor(h);
        for (int row = 0; row < h; ++row)
        {
            float rowFrac = (h > 1)
                                ? static_cast<float>(row) / static_cast<float>(h - 1)
                                : 0.0f;
            if (rowFrac < 0.5f)
                rowColor[row] = lerpColor(_theme.visBarLow, _theme.visBarMid, rowFrac * 2.0f);
            else
                rowColor[row] = lerpColor(_theme.visBarMid, _theme.visBarHigh, (rowFrac - 0.5f) * 2.0f);
        }

        for (int i = 0; i < totalBars; ++i)
        {
            int bx0 = x + (i * w) / totalBars;
            int bx1 = x + ((i + 1) * w) / totalBars;
            int barW = bx1 - bx0;
            if (barW < 1)
                continue;
            int bx = bx0;
            float val = _barValues[i];

            // Map value to height in 1/8 increments
            float heightF = val * static_cast<float>(h) * 8.0f;
            int fullRows = static_cast<int>(heightF) / 8;
            int partial = static_cast<int>(heightF) % 8;

            // Only the lit portion of the bar is painted. The root window is
            // cleared to the theme background each frame, so vacated cells
            // already read as empty — no trail/ghost layer is rendered.
            int const lastRow = std::min(h, fullRows + (partial > 0 ? 1 : 0));
            for (int row = 0; row < lastRow; ++row)
            {
                int const drawY = y + h - 1 - row;
                char32_t ch;
                if (row < fullRows)        ch = ventty::VBAR[8];
                else                       ch = ventty::VBAR[partial];

                ventty::Style style{rowColor[row], bg};
                for (int bw = 0; bw < barW && bx + bw < x + w; ++bw)
                {
                    window.putChar(bx + bw, drawY, ch, style);
                }
            }
        }
    }

} // namespace vtplayer
