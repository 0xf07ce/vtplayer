// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Visualizer.h"

#include <array>

namespace vtplayer
{

    /// Single-trace waveform plot, vertically centered. Rendered with Braille
    /// sub-pixels (2x4 per cell) so the trace is finer than a glyph grid.
    /// Serves as the slot-0 reference visualizer.
    class Oscilloscope : public Visualizer
    {
    public:
        void update(AudioEngine const &engine) override;
        void draw(ventty::Window &window, int x, int y, int w, int h) override;
        void setTheme(Theme const &theme) override { _theme = theme; }

    private:
        Theme _theme;
        std::array<float, 512> _samples{};
    };

} // namespace vtplayer
