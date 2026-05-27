// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "MatrixRain.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace vtplayer
{

    namespace
    {
        // Curated Unicode glyph pool. Only single-cell-wide codepoints —
        // double-width characters (CJK ideographs, Hangul syllables,
        // fullwidth forms) would shift columns by 1 cell and break the
        // grid. Emoji / pictographs are also excluded per the spec.
        std::vector<char32_t> const &glyphPool()
        {
            static std::vector<char32_t> const pool = []
            {
                std::vector<char32_t> v;
                v.reserve(512);
                auto add = [&](std::u32string_view s)
                {
                    for (char32_t c : s) v.push_back(c);
                };
                auto addRange = [&](char32_t lo, char32_t hi)
                {
                    for (char32_t c = lo; c <= hi; ++c) v.push_back(c);
                };

                // CP437-era ASCII printable, weighted by repetition since
                // these are the workhorse "digital rain" glyphs.
                std::u32string_view ascii =
                    U"0123456789!@#$%^&*+-=:;.<>?/~`|\\";
                for (int rep = 0; rep < 3; ++rep) add(ascii);

                // Latin-1 supplement non-letter symbols
                add(U"¡¢£¤¥¦§¨©ª«¬®¯°±²³´µ¶·¸¹º»¼½¾¿×÷");
                // Latin-1 letters with diacritics (visual variety)
                add(U"ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖØÙÚÛÜÝÞß");
                add(U"àáâãäåæçèéêëìíîïðñòóôõöøùúûüýþÿ");
                // Latin Extended-A (selective)
                add(U"ĀāĂăĄąĆćĈĉĊċČčĎďĐđĒēĔĕĘęĚěĞğĠġ");
                // IPA Extensions — narrow, visually exotic
                addRange(0x0250, 0x02AF);
                // Greek and Coptic
                add(U"ΑΒΓΔΕΖΗΘΙΚΛΜΝΞΟΠΡΣΤΥΦΧΨΩ");
                add(U"αβγδεζηθικλμνξοπρστυφχψω");
                // Cyrillic (skip Latin-lookalikes)
                add(U"БГДЁЖЗИЙЛПУФЦЧШЩЪЬЫЭЮЯ");
                add(U"бвгджзийклмнпртфцчшщъьыэюя");
                // Halfwidth Katakana — canonical Matrix-rain glyph block
                addRange(0xFF66, 0xFF9F);
                // Mathematical Operators (selective; non-emoji)
                add(U"∀∂∃∅∆∇∈∉∋∏∑−∗∘∙√∝∞∠∡∢∧∨∩∪∫≈≠≡≤≥⊂⊃⊆⊇⊕⊗⊥⋅");
                // Arrows
                add(U"←↑→↓↔↕↖↗↘↙↚↛↜↝⇐⇑⇒⇓⇔⇕");
                // Box drawing & light blocks (no full block — too heavy)
                add(U"─│┌┐└┘├┤┬┴┼╔╗╚╝╠╣╦╩╬╭╮╯╰");
                add(U"▏▎▍▌▋▊▉░▒");
                // General punctuation extras
                add(U"§¶†‡‰‱′″‴‹›※¦");

                return v;
            }();
            return pool;
        }

        // === Step 2 knobs (transient-driven) ===
        // pSpawn 은 절대 음량이 아니라 "현재 인텐시티가 slow average
        // 보다 얼마나 위인가" (excess) 의 함수. 평탄한 사운드는 slow
        // average 가 따라잡아 excess→0 → spawn 멈춤. 킥처럼 instantaneous
        // 한 솟음만 spawn 을 유발 → 음악적 변화가 시각적으로 직결됨.
        constexpr float kPeakGain = 0.2625f;       // pSpawn per unit excess
        constexpr float kSpawnCeiling = 0.0375f;   // pSpawn cap on extreme peaks
        // Slow-average release coefficient. 0.995 ≈ 200-frame TC ≈ 3.3s.
        // The baseline holds onto past loudness for several seconds so a
        // sudden kick produces a real excess gap (vs. 0.97 which would
        // catch up within the kick's own duration and flatten excess).
        constexpr float kSlowAvgRelease = 0.995f;
        // 같은 컬럼에서 다음 드롭이 spawn 되려면 기존 드롭의 head 가
        // 최소 이만큼은 내려가 있어야 함. 한 컬럼에 여러 드롭이 동시에
        // 떨어질 수 있게 해주는 핵심 노브.
        constexpr float kMinSpawnGap = 5.0f;

        // === Audio pipeline knobs ===
        constexpr float kPi = 3.14159265358979323846f;
        // dB → [0, 1] mapping for the bass-band sum. Range chosen so
        // typical bass-heavy music lands mid-range without pegging the
        // intensity at 1.0. Was -20..20 (clipped); shifted up to
        // accommodate the actual magnitudes the FFT produces.
        constexpr float kDbFloor = 0.0f;     // raw=0 below this dB
        constexpr float kDbCeiling = 50.0f;  // raw=1 above this dB
        // Envelope follower release coefficient (per frame). Lower = faster
        // decay when volume drops. 0.40 ≈ 18ms TC — sharp drop-off so the
        // rain reacts visibly to mid-track volume changes.
        constexpr float kIntensityRelease = 0.40f;
        // Intensity decay applied each frame the engine isn't playing.
        constexpr float kIntensityIdleDecay = 0.85f;
        // 속도 범위: uniform-random speed per drop (rows / frame).
        constexpr float kSpeedMin = 0.0625f;
        constexpr float kSpeedMax = 0.3125f;
        // === Step 3 knobs (intensity → speed) ===
        // 새 drop 의 기준속도(uniSpeed) 에 [kSpeedFactorMin..kSpeedFactorMax]
        // 사이의 스케일을 곱한다. I=0 (조용함) → kSpeedFactorMin,
        // I=1 (피크) → kSpeedFactorMax. 진행 중인 drop 의 속도는
        // 건드리지 않으므로 시뮬레이션 안정성에 영향 없음.
        constexpr float kSpeedFactorMin = 0.7f;
        constexpr float kSpeedFactorMax = 1.6f;
        // === Step 4 knobs (intensity → brightness) ===
        // spawn 시점의 I 에 비례한 brightness 스칼라를 drop 에 박아두고,
        // drop 이 적은 모든 셀의 색상을 background 쪽으로 lerp 해 어둡게.
        // I=0 → kBrightnessMin (조용한 drop 은 흐릿),
        // I=1 → kBrightnessMax (피크 drop 은 풀 컬러).
        constexpr float kBrightnessMin = 0.45f;
        constexpr float kBrightnessMax = 1.0f;

        // === Step 5 knobs (BPM beat sync) ===
        // 비트 검출: excess = intensity - slowAverage 의 rising edge.
        // - kOnsetThreshold: excess 가 이만큼 이상이 되어야 onset 으로 간주.
        //   너무 낮으면 noise 가 비트로 잡히고, 너무 높으면 약한 비트를 놓침.
        // - kMinBeatGapFrames: 18 프레임 ≈ 300 ms (60 fps 가정), 200 BPM cap.
        //   같은 킥의 ringing 이 더블트리거 되는 걸 막는 hysteresis.
        // - kBeatGapStaleFrames: 240 프레임 ≈ 4 s. 이 이상 침묵하면 직전 IBI
        //   를 무효화 (트랙 전환 / 긴 break 후 IBI 가 비현실적으로 길어지는
        //   상황 방지).
        constexpr float kOnsetThreshold = 0.08f;
        constexpr int kMinBeatGapFrames = 18;
        constexpr int kBeatGapStaleFrames = 240;
        // 비트가 잡히면 풀브라이트니스 drop 을 이만큼의 랜덤 컬럼에 동시 spawn.
        // 기존 transient pSpawn 과 결합해 "팟" 하는 동기화된 시각 신호를 만듦.
        constexpr int kBeatBurstCount = 6;
        // BPM 추정용 IBI(EMA) 평활화 계수. 큰 값일수록 느리게 갱신.
        constexpr float kBpmSmoothing = 0.7f;
        // 프레임 → BPM 환산용 가정 frame rate (Application 의 메인루프 ≈ 60 fps).
        // 절대 BPM 정밀도가 아닌 시각 동기 용도라 가정값으로 충분.
        constexpr float kAssumedFps = 60.0f;
        // 꼬리 길이: uniform-random tail length per drop. Shorter trails
        // mean fewer non-empty cells in steady state — both the aging loop
        // in stepSimulation() and the putChar pass in draw() scale with the
        // count of lit cells, so halving the tail roughly halves their
        // per-frame cost.
        constexpr int kTailMin = 3;
        constexpr int kTailMax = 9;
        // 글자 셔머: per-column shimmer-attempt probability per frame.
        // Each fired attempt does a reservoir-sampling pass over all
        // active cells in the column, so this is the second-biggest knob
        // after tail length. Lowered to reduce per-frame RNG work.
        constexpr float kShimmerProb = 0.04f;

        // 머리 강조: head 색상을 밝은 녹색 쪽으로 lerp 하는 비율
        // (0 = theme color 그대로, 1 = 완전 bright green).
        constexpr float kHeadBoost = 0.50f;
        // 꼬리 깊이: trail 종착점을 theme.matrixTail 보다 얼마나 더
        // 어둡게 끌어내릴지 (0 = 완전 검정, 1 = theme.matrixTail 그대로).
        // 작을수록 그라데이션의 폭이 넓어져 꼬리쪽 어둠이 더 뚜렷.
        constexpr float kTailDeepFactor = 0.50f;
        // 본체 디밍: body 색상을 배경 쪽으로 끌어내려 새로 추가되는
        // 글자가 전반적으로 어둡게 보이도록. (0 = 배경, 1 = theme body)
        constexpr float kBodyDim = 0.55f;
    } // namespace

    MatrixRain::MatrixRain()
        : _rng(std::random_device{}())
    {
        rebuildColorLut();
    }

    void MatrixRain::setTheme(Theme const &theme)
    {
        _theme = theme;
        rebuildColorLut();
    }

    void MatrixRain::rebuildColorLut()
    {
        // Bake the three theme-derived endpoint colors that draw() used to
        // recompute every frame.
        Color const bg = _theme.background;
        Color const headBright =
            lerpColor(_theme.matrixHead, Color{0x40, 0xFF, 0x40}, kHeadBoost);
        Color const bodyDim = lerpColor(bg, _theme.matrixBody, kBodyDim);
        Color const trailEnd =
            lerpColor(Color{0, 0, 0}, _theme.matrixTail, kTailDeepFactor);

        for (int b = 0; b < kBrightnessLevels; ++b)
        {
            float const bright =
                static_cast<float>(b) / static_cast<float>(kBrightnessLevels - 1);
            _headLut[b] = lerpColor(bg, headBright, bright);
            for (int f = 0; f < kFadeLevels; ++f)
            {
                float const linT =
                    static_cast<float>(f) / static_cast<float>(kFadeLevels - 1);
                float const t = std::sqrt(linT);
                Color const trail = lerpColor(bodyDim, trailEnd, t);
                _trailLut[b][f] = lerpColor(bg, trail, bright);
            }
        }
    }

    char32_t MatrixRain::randomGlyph()
    {
        auto const &pool = glyphPool();
        std::uniform_int_distribution<size_t> d(0, pool.size() - 1);
        return pool[d(_rng)];
    }

    void MatrixRain::resizeGrid(int w, int h)
    {
        _gridW = w;
        _gridH = h;
        _cols.assign(static_cast<size_t>(std::max(0, w)), Column{});
        for (auto &c : _cols)
        {
            c.glyph.assign(static_cast<size_t>(std::max(0, h)), 0);
            c.age.assign(static_cast<size_t>(std::max(0, h)), 0);
            c.cellMaxAge.assign(static_cast<size_t>(std::max(0, h)), 0);
            c.cellBrightness.assign(static_cast<size_t>(std::max(0, h)), 0.0f);
        }
    }

    void MatrixRain::update(AudioEngine const &engine)
    {
        // Engine not actively pushing samples → bleed everything toward zero
        // so the rain quiets down on pause / track-end.
        if (engine.state() != PlayState::Playing)
        {
            _intensity *= kIntensityIdleDecay;
            _slowAverage *= kIntensityIdleDecay;
            // Step 5: stale beat state on pause / stop. Next play starts
            // fresh — IBI from before the pause shouldn't poison BPM.
            _prevExcess = 0.0f;
            _framesSinceBeat = kBeatGapStaleFrames;
            _beatIntervalEma = 0.0f;
            _bpm = 0.0f;
            _beatPending = false;
            return;
        }

        float samples[512];
        engine.getSamples(samples, 512);

        // Hann window
        for (int i = 0; i < 512; ++i)
        {
            float t = static_cast<float>(i) / 511.0f;
            float hann = 0.5f * (1.0f - std::cos(2.0f * kPi * t));
            samples[i] *= hann;
        }
        std::complex<float> fftOut[512];
        _fft.forward(samples, fftOut);
        float mag[256];
        _fft.magnitude(fftOut, mag);

        // Sum bins 1..12 (skip DC). At Fs = 44.1 kHz this covers
        // ~85–1030 Hz — kick + low end of bass instruments.
        float sum = 0.0f;
        for (int b = 1; b <= 12; ++b)
            sum += mag[b];

        float db = 20.0f * std::log10(sum + 1e-6f);
        float raw = std::clamp((db - kDbFloor) / (kDbCeiling - kDbFloor), 0.0f, 1.0f);

        // Envelope follower: instant attack lets each kick spike the
        // intensity immediately; slow release keeps the spawn boost
        // visible for ~100 ms after the hit.
        if (raw > _intensity)
            _intensity = raw;
        else
            _intensity = _intensity * kIntensityRelease + raw * (1.0f - kIntensityRelease);

        // Slow EMA tracks the longer-term loudness baseline. Peaks above
        // it = transients to react to.
        _slowAverage = _slowAverage * kSlowAvgRelease + _intensity * (1.0f - kSlowAvgRelease);

        // Step 5: beat onset detection. Rising edge on the excess signal
        // (intensity − slowAverage), gated by a min-gap so a single kick
        // can't double-trigger.
        float const excess = std::max(0.0f, _intensity - _slowAverage);
        bool const risingEdge = (excess >= kOnsetThreshold && _prevExcess < kOnsetThreshold);
        bool const gapClear = (_framesSinceBeat >= kMinBeatGapFrames);
        if (risingEdge && gapClear)
        {
            // Update IBI / BPM only if the previous beat is recent enough
            // — long gaps (> kBeatGapStaleFrames) are likely a track
            // change or rest, not a real interval.
            if (_framesSinceBeat < kBeatGapStaleFrames)
            {
                float const interval = static_cast<float>(_framesSinceBeat);
                _beatIntervalEma = (_beatIntervalEma <= 0.0f)
                    ? interval
                    : _beatIntervalEma * kBpmSmoothing + interval * (1.0f - kBpmSmoothing);
                _bpm = (60.0f * kAssumedFps) / std::max(1.0f, _beatIntervalEma);
            }
            _framesSinceBeat = 0;
            _beatPending = true;
        }
        _prevExcess = excess;
        if (_framesSinceBeat < 100000)
            ++_framesSinceBeat;
    }

    void MatrixRain::stepSimulation(int w, int h)
    {
        if (w != _gridW || h != _gridH)
            resizeGrid(w, h);
        if (w <= 0 || h <= 0) return;

        std::uniform_real_distribution<float> uni01(0.0f, 1.0f);
        std::uniform_real_distribution<float> uniSpeed(kSpeedMin, kSpeedMax);
        std::uniform_int_distribution<int> uniTail(kTailMin, kTailMax);
        std::uniform_real_distribution<float> uniHeadStart(-3.0f, 0.0f);
        std::uniform_int_distribution<int> uniRow(0, std::max(0, h - 1));

        // Transient-driven spawn: pSpawn is a function of how far the
        // instantaneous intensity exceeds the slow-tracked baseline. A
        // sustained loud passage produces NO spawning once slowAverage
        // catches up — only fresh peaks (kicks) trigger drops.
        float const I = std::clamp(_intensity, 0.0f, 1.0f);
        float const baseline = std::clamp(_slowAverage, 0.0f, 1.0f);
        float const excess = std::max(0.0f, I - baseline);
        float const pSpawn = std::min(excess * kPeakGain, kSpawnCeiling);

        // Step 5: beat burst — when update() detected an onset, drop a
        // handful of full-brightness drops into random columns at once.
        // This is what makes beats *visible* as a synchronized event,
        // distinct from the continuous trickle of transient-driven
        // pSpawn (Step 2). Burst drops still respect kMinSpawnGap on
        // each picked column to avoid stacking on a fresh head.
        if (_beatPending && !_cols.empty())
        {
            std::uniform_int_distribution<int> uniCol(0, w - 1);
            for (int i = 0; i < kBeatBurstCount; ++i)
            {
                int const col = uniCol(_rng);
                auto &cb = _cols[static_cast<size_t>(col)];
                bool gapOk = true;
                for (auto const &d : cb.drops)
                {
                    if (d.headY < kMinSpawnGap) { gapOk = false; break; }
                }
                if (!gapOk) continue;

                Drop d;
                d.headY = uniHeadStart(_rng);
                float const speedFactor =
                    kSpeedFactorMin + I * (kSpeedFactorMax - kSpeedFactorMin);
                d.speed = uniSpeed(_rng) * speedFactor;
                d.brightness = 1.0f; // beat drops always full-bright
                int const tailRows = uniTail(_rng);
                d.maxAge =
                    static_cast<int>(static_cast<float>(tailRows) / d.speed) + 4;
                cb.drops.push_back(d);
            }
            _beatPending = false;
        }

        for (auto &c : _cols)
        {
            // 1) Age existing glyphs and erase those past their fade window.
            //    Each cell has its own max-age recorded by the drop that
            //    wrote it, so trails fade according to the original drop's
            //    speed even after that drop has been removed.
            for (int r = 0; r < h; ++r)
            {
                if (c.glyph[r] != 0)
                {
                    ++c.age[r];
                    if (c.age[r] > c.cellMaxAge[r])
                    {
                        c.glyph[r] = 0;
                        c.age[r] = 0;
                        c.cellMaxAge[r] = 0;
                        c.cellBrightness[r] = 0.0f;
                    }
                }
            }

            // 2) Walk every active drop. Each places a fresh glyph at
            //    every row its head crosses this frame; later drops that
            //    overlap with earlier trails just reset age to 0.
            for (auto &d : c.drops)
            {
                int prevHead = static_cast<int>(std::floor(d.headY));
                d.headY += d.speed;
                int newHead = static_cast<int>(std::floor(d.headY));
                for (int r = std::max(0, prevHead + 1); r <= newHead && r < h; ++r)
                {
                    c.glyph[r] = randomGlyph();
                    c.age[r] = 0;
                    c.cellMaxAge[r] = d.maxAge;
                    c.cellBrightness[r] = d.brightness;
                }
                // Keep the head bright while it sits on a row. At speed
                // < 1.0 the head only crosses to a new row every few
                // frames; without this reset, the head glyph ages out of
                // head color between row transitions and "blinks".
                if (newHead >= 0 && newHead < h && c.glyph[newHead] != 0)
                    c.age[newHead] = 0;
            }
            // Remove drops whose head has walked past the bottom — their
            // trail keeps aging out independently via cellMaxAge.
            c.drops.erase(
                std::remove_if(c.drops.begin(), c.drops.end(),
                               [h](Drop const &d) {
                                   return static_cast<int>(std::floor(d.headY)) >= h;
                               }),
                c.drops.end());

            // 3) Spawn a new drop with intensity-driven probability —
            //    gated so the youngest existing drop has already moved at
            //    least kMinSpawnGap rows down, leaving visible separation.
            bool gapClear = true;
            for (auto const &d : c.drops)
            {
                if (d.headY < kMinSpawnGap)
                {
                    gapClear = false;
                    break;
                }
            }
            if (gapClear && uni01(_rng) < pSpawn)
            {
                Drop d;
                d.headY = uniHeadStart(_rng);
                // Step 3: spawn 시점의 인텐시티에 비례해 속도를 스케일.
                // 잔잔할 땐 천천히, 킥에서 spawn 된 drop 은 빠르게.
                float const speedFactor =
                    kSpeedFactorMin + I * (kSpeedFactorMax - kSpeedFactorMin);
                d.speed = uniSpeed(_rng) * speedFactor;
                // Step 4: spawn 시점의 인텐시티를 brightness 로 박아둔다.
                // drop 이 적은 모든 셀이 이 값을 그대로 받는다.
                d.brightness =
                    kBrightnessMin + I * (kBrightnessMax - kBrightnessMin);
                int tailRows = uniTail(_rng);
                d.maxAge = static_cast<int>(static_cast<float>(tailRows) / d.speed) + 4;
                c.drops.push_back(d);
            }

            // 4) Shimmer: occasionally replace a glyph in the trail.
            // Pick only from active (non-empty, age > 0) cells so every
            // attempt produces a visible mutation. The head (age == 0)
            // is excluded so the brightest cell stays stable.
            //
            // Reservoir sampling (k=1) — one pass, uniform over active
            // cells. Replaces the previous count-then-pick double scan.
            if (uni01(_rng) < kShimmerProb)
            {
                int seen = 0;
                int chosen = -1;
                for (int r = 0; r < h; ++r)
                {
                    if (c.glyph[r] == 0 || c.age[r] == 0) continue;
                    ++seen;
                    std::uniform_int_distribution<int> pick(0, seen - 1);
                    if (pick(_rng) == 0) chosen = r;
                }
                if (chosen >= 0)
                    c.glyph[chosen] = randomGlyph();
            }
        }
    }

    void MatrixRain::draw(ventty::Window &window, int x, int y, int w, int h)
    {
        stepSimulation(w, h);
        if (w <= 0 || h <= 0 || _cols.empty())
            return;

        Color const bg = _theme.background;
        // Application clears the root window with the theme background each
        // frame, so empty cells are already in the right state — we don't
        // need to paint them. That alone removes ~½ of the putChar calls on
        // a sparse rain frame.

        constexpr int kBrightMax = kBrightnessLevels - 1;
        constexpr int kFadeMax = kFadeLevels - 1;

        for (int col = 0; col < w && col < static_cast<int>(_cols.size()); ++col)
        {
            auto const &c = _cols[col];

            for (int r = 0; r < h; ++r)
            {
                if (c.glyph[r] == 0)
                    continue;

                // Quantize cellBrightness into [0..kBrightMax]. Snapshot
                // brightness is already discretized by spawn events, but
                // the lerp-toward-bg step reduces it to a continuous float;
                // bin it so adjacent cells cluster onto the same Color.
                float const bright = std::clamp(c.cellBrightness[r], 0.0f, 1.0f);
                int bIdx = static_cast<int>(bright * kBrightMax + 0.5f);
                if (bIdx < 0) bIdx = 0;
                else if (bIdx > kBrightMax) bIdx = kBrightMax;

                Color fg;
                if (c.age[r] == 0)
                {
                    fg = _headLut[bIdx];
                }
                else
                {
                    // linT = age / maxAge, then quantize. Integer rounding
                    // avoids the per-cell sqrt/divide that the LUT bakes.
                    int const denom = std::max(1, c.cellMaxAge[r]);
                    int fIdx = (c.age[r] * kFadeMax + denom / 2) / denom;
                    if (fIdx < 0) fIdx = 0;
                    else if (fIdx > kFadeMax) fIdx = kFadeMax;
                    fg = _trailLut[bIdx][fIdx];
                }

                ventty::Style const style{fg, bg};
                window.putChar(x + col, y + r, c.glyph[r], style);
            }
        }
    }

} // namespace vtplayer
