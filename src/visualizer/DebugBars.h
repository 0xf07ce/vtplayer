// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Visualizer.h"
#include "../audio/Fft.h"

#include <array>

namespace vtplayer
{

    /// Debug-style horizontal bar chart of the intermediate audio-analysis
    /// values that visualizers 0/1/2 derive — one metric per line, drawn
    /// with `=` characters and a curated earth-tone palette so each row
    /// has its own stable color.
    ///
    /// Surfaces (top to bottom):
    ///   peak / rms              — time-domain envelope
    ///   bass / mid / treb / cent — spectral bands + centroid
    ///   I / slow / xs / pSp     — MatrixRain intensity envelope
    ///   bpm / beat              — onset / tempo state
    ///   gain / pos%             — engine state
    class DebugBars : public Visualizer
    {
    public:
        DebugBars();

        void update(AudioEngine const &engine) override;
        void draw(ventty::Window &window, int x, int y, int w, int h) override;
        void setTheme(Theme const &theme) override { _theme = theme; }

    private:
        struct Metric
        {
            char const *label = "";  ///< 4-char (or fewer) abbreviation
            Color color;             ///< per-row bar color
            float value = 0.0f;      ///< normalized [0, 1]
        };

        Theme _theme;
        Fft<512> _fft;

        // MatrixRain intensity-envelope mirror — recomputed here so this
        // visualizer is self-contained (the other visualizer instance is
        // gone the moment the user switches slots).
        float _intensity = 0.0f;
        float _slowAverage = 0.0f;
        float _prevExcess = 0.0f;
        int _framesSinceBeat = 10000;
        float _beatIntervalEma = 0.0f;
        float _bpm = 0.0f;
        float _beatPulse = 0.0f; ///< snap to 1 on onset, exponential decay

        // Smoothed time/spectral domain stats (independent EMA so the bars
        // don't strobe at the audio frame rate).
        float _peak = 0.0f;
        float _rms = 0.0f;
        float _bass = 0.0f;
        float _mid = 0.0f;
        float _treb = 0.0f;
        float _centroid = 0.0f;

        static constexpr int kNumMetrics = 14;
        std::array<Metric, kNumMetrics> _metrics{};
    };

} // namespace vtplayer
