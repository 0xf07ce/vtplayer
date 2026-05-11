// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "DebugBars.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string_view>

namespace vtplayer
{

    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;

        // Earth-tone palette — muted, warm, harmonious. Loosely modeled on
        // designer earth-tone palettes (terracotta / sage / ochre family).
        // 16 entries so each metric row gets its own stable color without
        // recycling. RGB values picked so adjacent rows differ enough in
        // hue to be readable while staying within the same tonal range.
        constexpr std::array<Color, 16> kPalette = {{
            {0xC9, 0x70, 0x64}, // terracotta
            {0xC9, 0xA0, 0x62}, // amber
            {0x8E, 0x9A, 0x77}, // sage
            {0xB2, 0x69, 0x48}, // rust
            {0xC8, 0xB5, 0x82}, // sand
            {0x6B, 0x87, 0x65}, // forest
            {0xB5, 0x85, 0x91}, // dusty rose
            {0xA8, 0xA8, 0x78}, // khaki
            {0xB0, 0x70, 0x60}, // sienna
            {0x8E, 0x76, 0x60}, // mocha
            {0xC9, 0x90, 0x57}, // ochre
            {0x78, 0x8B, 0x7E}, // slate sage
            {0x9C, 0x8A, 0x6E}, // taupe
            {0xC4, 0x8E, 0x6E}, // peach
            {0x8E, 0x8B, 0x82}, // pewter
            {0x70, 0x80, 0x6F}, // moss
        }};

        // === MatrixRain intensity-envelope mirror ===
        // Constants duplicated from MatrixRain.cpp on purpose: this is a
        // debug view of those exact derivations, so any drift in the source
        // would silently invalidate the displayed values. Keep these in
        // sync manually if MatrixRain's pipeline changes.
        constexpr float kDbFloor = 0.0f;
        constexpr float kDbCeiling = 50.0f;
        constexpr float kIntensityRelease = 0.40f;
        constexpr float kIntensityIdleDecay = 0.85f;
        constexpr float kSlowAvgRelease = 0.995f;
        constexpr float kOnsetThreshold = 0.08f;
        constexpr int kMinBeatGapFrames = 18;
        constexpr int kBeatGapStaleFrames = 240;
        constexpr float kBpmSmoothing = 0.7f;
        constexpr float kAssumedFps = 60.0f;
        constexpr float kPeakGain = 0.2625f;
        constexpr float kSpawnCeiling = 0.0375f;

        // Beat onset pulse: snap to 1.0 on detected onset, decay each
        // frame so the bar shows visible pulses without latching.
        constexpr float kBeatPulseDecay = 0.85f;

        // Generic EMA release for raw band/peak/rms — fast enough to track
        // music dynamics, slow enough to suppress sample-rate strobing.
        constexpr float kBandRelease = 0.70f;

        constexpr int kFftBins = 256;

        constexpr int kLabelChars = 4;     ///< label column width
        constexpr int kPrefixChars = 5;    ///< label + 1 spacer
        constexpr int kSuffixChars = 5;    ///< 1 spacer + 4-char value
        constexpr int kMinBarWidth = 4;    ///< below this we skip the value column

        float normDb(float sum)
        {
            float const db = 20.0f * std::log10(sum + 1e-6f);
            return std::clamp((db - kDbFloor) / (kDbCeiling - kDbFloor), 0.0f, 1.0f);
        }
    } // namespace

    DebugBars::DebugBars()
    {
        // Order is the on-screen vertical order — rearrange here to change
        // the layout. Labels stay <= 4 chars to fit the label column.
        static constexpr char const *kLabels[kNumMetrics] = {
            "gain", "pos%",
            "peak", "rms",
            "bass", "mid", "treb", "cent",
            "I", "slow", "xs", "pSp",
            "bpm", "beat",
        };
        for (int i = 0; i < kNumMetrics; ++i)
        {
            _metrics[i].label = kLabels[i];
            _metrics[i].color = kPalette[i];
            _metrics[i].value = 0.0f;
        }
    }

    void DebugBars::update(AudioEngine const &engine)
    {
        // Engine-level metrics that don't depend on FFT — same across
        // playing/idle, so compute up front.
        PlayState const ps = engine.state();
        // Auto-gain in dB. Center 0 dB at 0.5; ±20 dB hits the bar edges.
        float const gainDb = engine.autoGainDb();
        float const gain = std::clamp((gainDb + 20.0f) / 40.0f, 0.0f, 1.0f);
        // Position fraction; guard duration=0 (radio / unknown length).
        float const dur = engine.duration();
        float const pos = (dur > 0.001f)
                              ? std::clamp(engine.position() / dur, 0.0f, 1.0f)
                              : 0.0f;

        if (ps != PlayState::Playing)
        {
            // Bleed every audio-derived value toward zero so the bars
            // visibly "go quiet" on pause / stop.
            _intensity *= kIntensityIdleDecay;
            _slowAverage *= kIntensityIdleDecay;
            _prevExcess = 0.0f;
            _framesSinceBeat = kBeatGapStaleFrames;
            _beatIntervalEma = 0.0f;
            _bpm = 0.0f;
            _beatPulse *= kBeatPulseDecay;

            _peak *= kIntensityIdleDecay;
            _rms *= kIntensityIdleDecay;
            _bass *= kIntensityIdleDecay;
            _mid *= kIntensityIdleDecay;
            _treb *= kIntensityIdleDecay;
            _centroid *= kIntensityIdleDecay;

            float const idleValues[kNumMetrics] = {
                gain, pos,
                _peak, _rms,
                _bass, _mid, _treb, _centroid,
                _intensity, _slowAverage, 0.0f, 0.0f,
                std::clamp(_bpm / 200.0f, 0.0f, 1.0f), _beatPulse,
            };
            for (int i = 0; i < kNumMetrics; ++i)
                _metrics[i].value = std::clamp(idleValues[i], 0.0f, 1.0f);
            return;
        }

        float samples[512];
        engine.getSamples(samples, 512);

        // Time-domain peak / RMS over the raw (un-windowed) buffer.
        float peakRaw = 0.0f;
        float sqSum = 0.0f;
        for (int i = 0; i < 512; ++i)
        {
            float const a = std::abs(samples[i]);
            if (a > peakRaw) peakRaw = a;
            sqSum += samples[i] * samples[i];
        }
        peakRaw = std::clamp(peakRaw, 0.0f, 1.0f);
        float const rmsRaw = std::clamp(std::sqrt(sqSum / 512.0f), 0.0f, 1.0f);
        _peak = std::max(peakRaw, _peak * kBandRelease);
        _rms = std::max(rmsRaw, _rms * kBandRelease);

        // Hann window then FFT — windowing in-place is fine since we've
        // already pulled out the time-domain stats above.
        for (int i = 0; i < 512; ++i)
        {
            float const t = static_cast<float>(i) / 511.0f;
            float const hann = 0.5f * (1.0f - std::cos(2.0f * kPi * t));
            samples[i] *= hann;
        }
        std::complex<float> fftOut[512];
        _fft.forward(samples, fftOut);
        float mag[kFftBins];
        _fft.magnitude(fftOut, mag);

        // Three coarse bands. Boundaries are arbitrary but chosen so each
        // band has comparable bin count after log-style perception, which
        // is good enough for an at-a-glance debug bar (not for analysis).
        float bassSum = 0.0f, midSum = 0.0f, trebSum = 0.0f;
        for (int b = 1; b <= 12; ++b) bassSum += mag[b];
        for (int b = 13; b <= 63; ++b) midSum += mag[b];
        for (int b = 64; b < kFftBins; ++b) trebSum += mag[b];
        float const bassN = normDb(bassSum);
        float const midN = normDb(midSum);
        float const trebN = normDb(trebSum);
        _bass = std::max(bassN, _bass * kBandRelease);
        _mid = std::max(midN, _mid * kBandRelease);
        _treb = std::max(trebN, _treb * kBandRelease);

        // Spectral centroid: sum(bin * mag) / sum(mag), normalized to [0, 1]
        // by dividing by the highest bin index. Tells you where the
        // spectral energy mass sits.
        float numerator = 0.0f;
        float denominator = 0.0f;
        for (int b = 1; b < kFftBins; ++b)
        {
            numerator += static_cast<float>(b) * mag[b];
            denominator += mag[b];
        }
        float const centRaw = (denominator > 1e-6f)
            ? std::clamp(numerator / denominator / static_cast<float>(kFftBins - 1), 0.0f, 1.0f)
            : 0.0f;
        _centroid = _centroid * 0.85f + centRaw * 0.15f;

        // === MatrixRain envelope mirror ===
        // bassN is exactly the same `raw` MatrixRain feeds to its envelope.
        if (bassN > _intensity)
            _intensity = bassN;
        else
            _intensity = _intensity * kIntensityRelease + bassN * (1.0f - kIntensityRelease);
        _slowAverage = _slowAverage * kSlowAvgRelease + _intensity * (1.0f - kSlowAvgRelease);

        float const excess = std::max(0.0f, _intensity - _slowAverage);
        bool const risingEdge =
            (excess >= kOnsetThreshold && _prevExcess < kOnsetThreshold);
        bool const gapClear = (_framesSinceBeat >= kMinBeatGapFrames);
        if (risingEdge && gapClear)
        {
            if (_framesSinceBeat < kBeatGapStaleFrames)
            {
                float const interval = static_cast<float>(_framesSinceBeat);
                _beatIntervalEma = (_beatIntervalEma <= 0.0f)
                    ? interval
                    : _beatIntervalEma * kBpmSmoothing + interval * (1.0f - kBpmSmoothing);
                _bpm = (60.0f * kAssumedFps) / std::max(1.0f, _beatIntervalEma);
            }
            _framesSinceBeat = 0;
            _beatPulse = 1.0f;
        }
        _prevExcess = excess;
        if (_framesSinceBeat < 100000) ++_framesSinceBeat;
        _beatPulse *= kBeatPulseDecay;

        float const pSpawn = std::min(excess * kPeakGain, kSpawnCeiling);
        // pSpawn maxes out at kSpawnCeiling — tiny on a [0, 1] scale, so
        // rescale to the ceiling for visibility.
        float const pSpawnNorm = std::clamp(pSpawn / kSpawnCeiling, 0.0f, 1.0f);

        float const values[kNumMetrics] = {
            gain, pos,
            _peak, _rms,
            _bass, _mid, _treb, _centroid,
            _intensity, _slowAverage, std::clamp(excess, 0.0f, 1.0f), pSpawnNorm,
            std::clamp(_bpm / 200.0f, 0.0f, 1.0f), std::clamp(_beatPulse, 0.0f, 1.0f),
        };
        for (int i = 0; i < kNumMetrics; ++i)
            _metrics[i].value = std::clamp(values[i], 0.0f, 1.0f);
    }

    void DebugBars::draw(ventty::Window &window, int x, int y, int w, int h)
    {
        if (w <= 0 || h <= 0)
            return;

        // Layout:
        //   "labl " | "===========" | " 0.XX"
        //   ←  5  → ←   barWidth →  ←  5  →
        //
        // If w is too small for the value column we still show the label
        // and bar, just dropping the trailing value text.
        bool const showValue = (w >= kPrefixChars + kMinBarWidth + kSuffixChars);
        int const reservedRight = showValue ? kSuffixChars : 0;
        int const barWidth = std::max(1, w - kPrefixChars - reservedRight);

        ventty::Style const labelStyle{_theme.foreground, _theme.background};

        int const rowsToShow = std::min(h, kNumMetrics);
        for (int i = 0; i < rowsToShow; ++i)
        {
            auto const &m = _metrics[i];
            int const drawY = y + i;

            // Label, padded to kLabelChars. snprintf %-4.4s left-aligns and
            // truncates so any future label longer than 4 still fits.
            char labelBuf[8];
            std::snprintf(labelBuf, sizeof(labelBuf), "%-*.*s", kLabelChars, kLabelChars, m.label);
            window.drawText(x, drawY, std::string_view(labelBuf, kLabelChars), labelStyle);

            // Bar — single style per row so the renderer's truecolor diff
            // cache emits the ANSI fg-set sequence once and then just runs
            // through `=` chars.
            ventty::Style const barStyle{m.color, _theme.background};
            int const fillCols = static_cast<int>(m.value * static_cast<float>(barWidth) + 0.5f);
            int const fill = std::clamp(fillCols, 0, barWidth);
            int const barX0 = x + kPrefixChars;
            for (int c = 0; c < fill; ++c)
                window.putChar(barX0 + c, drawY, U'=', barStyle);

            // Right-aligned numeric readout, e.g. "0.42". Leading space is
            // a visual gap from the bar's tip.
            if (showValue)
            {
                char valBuf[8];
                std::snprintf(valBuf, sizeof(valBuf), "%4.2f", m.value);
                window.drawText(x + w - 4, drawY,
                                std::string_view(valBuf, 4), labelStyle);
            }
        }
    }

} // namespace vtplayer
