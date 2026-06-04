// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Visualizer.h"
#include "../audio/Fft.h"

#include <array>
#include <random>
#include <vector>

namespace vtplayer
{

    /// Matrix-style "digital rain" visualizer.
    ///
    /// Step 5: spawn probability + new-drop speed + new-drop brightness
    /// scale with smoothed bass intensity (steps 2–4), AND beat onsets
    /// trigger a burst-spawn of full-brightness drops across random
    /// columns. Inter-onset intervals feed an EMA-smoothed BPM estimate.
    class MatrixRain : public Visualizer
    {
    public:
        MatrixRain();

        void update(AudioEngine const &engine) override;
        void draw(ventty::Window &window, int x, int y, int w, int h) override;
        void setTheme(Theme const &theme) override;
        int preferredFps() const override { return 20; }

    private:
        // Color quantization levels. Adjacent cells that fall into the same
        // (brightness, fade) bin share an identical Color, so the terminal
        // renderer's per-cell ANSI truecolor diff cache hits and the heavy
        // \033[38;2;R;G;Bm \033[48;2;R;G;Bm sequence is emitted once per run
        // instead of once per cell.
        static constexpr int kBrightnessLevels = 8;
        static constexpr int kFadeLevels = 8;

        struct Drop
        {
            float headY = 0.0f;      ///< current head position (row, fractional)
            float speed = 0.5f;      ///< rows advanced per frame
            int maxAge = 24;         ///< target trail length in frames
            float brightness = 1.0f; ///< scalar [0..1], snapshot of intensity at spawn
        };

        struct Column
        {
            std::vector<char32_t> glyph;        ///< per-row glyph (0 = empty cell)
            std::vector<int> age;               ///< frames since glyph was placed
            std::vector<int> cellMaxAge;        ///< per-cell max age, set by the drop that wrote it
            std::vector<float> cellBrightness;  ///< per-cell brightness, set by the drop that wrote it
            std::vector<Drop> drops;            ///< zero or more concurrently walking drops
        };

        Theme _theme;
        std::vector<Column> _cols;
        int _gridW = 0;
        int _gridH = 0;
        std::mt19937 _rng;

        // Precomputed color tables — rebuilt only in setTheme(), reused for
        // every cell every frame. headLut[b]: head-row color at brightness
        // bin b. trailLut[b][f]: trail color at brightness bin b, fade bin f.
        std::array<Color, kBrightnessLevels> _headLut{};
        std::array<std::array<Color, kFadeLevels>, kBrightnessLevels> _trailLut{};

        // Audio: smoothed bass intensity in [0, 1].
        Fft<512> _fft;
        float _intensity = 0.0f;
        float _slowAverage = 0.0f; ///< long EMA — baseline for transient detection

        // Beat detection state (Step 5).
        float _prevExcess = 0.0f;       ///< previous frame's (intensity - slowAverage)
        int _framesSinceBeat = 10000;   ///< frames since last detected beat onset
        float _beatIntervalEma = 0.0f;  ///< EMA of inter-onset interval (frames)
        float _bpm = 0.0f;              ///< current BPM estimate (info / future use)
        bool _beatPending = false;      ///< set by update(), consumed by stepSimulation()

        /// Returns a random glyph from the curated 1-cell-wide Unicode pool.
        char32_t randomGlyph();

        /// Reset all column state to match (w, h).
        void resizeGrid(int w, int h);

        /// Per-frame simulation step. (w, h) come from draw() since that's
        /// the only place we learn the visible region.
        void stepSimulation(int w, int h);

        /// Rebuild _headLut / _trailLut from the current theme.
        void rebuildColorLut();
    };

} // namespace vtplayer
