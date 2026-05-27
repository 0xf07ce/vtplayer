// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Visualizer.h"
#include "../audio/Fft.h"

#include <vector>

namespace vtplayer
{

    /// Spectrum analyzer visualizer using vertical bars with color gradient.
    class AudioSpectrum : public Visualizer
    {
    public:
        explicit AudioSpectrum(int numBars = 48);

        void update(AudioEngine const &engine) override;
        void draw(ventty::Window &window, int x, int y, int w, int h) override;
        void setTheme(Theme const &theme) override { _theme = theme; }

    private:
        Theme _theme;
        Fft<512> _fft;

        std::vector<float> _barValues;
        std::vector<float> _peakValues;
    };

} // namespace vtplayer
