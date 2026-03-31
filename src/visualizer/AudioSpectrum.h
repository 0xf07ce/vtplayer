// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Visualizer.h"
#include "../audio/Fft.h"

#include <array>

namespace vtamp
{

    /// Spectrum analyzer visualizer using vertical bars with color gradient.
    class AudioSpectrum : public Visualizer
    {
    public:
        void update(AudioEngine const &engine) override;
        void draw(ventty::Window &window, int x, int y, int w, int h) override;
        void setTheme(Theme const &theme) override { _theme = theme; }

    private:
        Theme _theme;
        Fft<512> _fft;

        static constexpr int NUM_BARS = 48;
        std::array<float, NUM_BARS> _barValues{};
        std::array<float, NUM_BARS> _peakValues{};
    };

} // namespace vtamp
