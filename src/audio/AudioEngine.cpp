// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

// miniaudio is used exclusively for cross-platform audio *output*
// (ma_device). All decoding is handled by libav via vtplayer::Decoder.
#define MA_LOG_LEVEL MA_LOG_LEVEL_ERROR
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio/miniaudio.h>

#include "AudioEngine.h"

#include "Decoder.h"
#include "ReplayGain.h"
#include "StreamSource.h"
#include "../util/UnicodeNormalize.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{

/// Soft-clip: linear pass-through for |x| ≤ knee, smooth exponential
/// approach to ±1.0 above. Used to tame peaks when volume > 1.0.
inline float softClip(float x)
{
    constexpr float knee = 0.8f;
    constexpr float headroom = 1.0f - knee; // 0.2
    float absx = std::fabs(x);
    if (absx <= knee) return x;
    float sign = x < 0.0f ? -1.0f : 1.0f;
    float over = absx - knee;
    return sign * (knee + headroom * (1.0f - std::exp(-over / headroom)));
}

// Gain-normalization tuning. Targets and limits are shared between the auto-gain
// RMS estimator and the ReplayGain-tag path; only the RMS path uses TargetRms /
// NoiseGate.
constexpr float kAutoGainTargetRms = 0.12589f;  ///< -18 dBFS (AG only)
constexpr float kAutoGainNoiseGate = 0.003162f; ///< -50 dBFS, skip updates below (AG only)
constexpr float kGainMaxLin = 3.981f;           ///< +12 dB ceiling
constexpr float kGainMinLin = 0.2512f;          ///< -12 dB floor
constexpr float kGainAttackSamples  = 0.15f * 44100.0f; ///< 150ms (when reducing gain)
constexpr float kGainReleaseSamples = 2.5f  * 44100.0f; ///< 2.5s (when raising gain)

} // namespace

namespace vtplayer
{

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    shutdown();
}

bool AudioEngine::init()
{
    _device = new ma_device;
    return true;
}

void AudioEngine::shutdown()
{
    stop();

    if (_device)
    {
        delete _device;
        _device = nullptr;
    }
}

bool AudioEngine::load(std::filesystem::path const & path)
{
    stop();
    _lastError.clear();

    _currentFormat = TrackInfo::formatFromPath(path);
    if (_currentFormat == AudioFormat::Unknown)
    {
        _lastError = "Unsupported format";
        return false;
    }

    _currentTrack.path = path;
    _currentTrack.format = _currentFormat;
    _currentTrack.title = vtplayer::toNfc(path.stem().string());
    _currentTrack.artist.clear();
    _currentTrack.duration = 0.0f;

    auto dec = std::make_unique<Decoder>();
    if (!dec->open(path.string(), false))
    {
        _lastError = dec->error();
        return false;
    }

    _currentTrack.duration = static_cast<float>(dec->duration());
    _duration.store(_currentTrack.duration, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(_audioMutex);
        _decoder = std::move(dec);
    }

    _framesPlayed = 0;
    _position.store(0.0f, std::memory_order_relaxed);

    // Resolve gain source for this track.
    ReplayGainInfo rg = readReplayGain(path);
    if (rg.hasTrackGain)
    {
        float linear = std::pow(10.0f, rg.trackGainDb / 20.0f);
        linear = std::clamp(linear, kGainMinLin, kGainMaxLin);
        _replayGainLinear.store(linear, std::memory_order_relaxed);
        _gainSource.store(GainSource::ReplayGain, std::memory_order_relaxed);
        // Snap to target when normalization is on, so the first sample is not
        // a ramp from unity (would briefly play at original loudness).
        bool normOn = _gainNormEnabled.load(std::memory_order_relaxed);
        _currentGain.store(normOn ? linear : 1.0f, std::memory_order_relaxed);
    }
    else
    {
        _replayGainLinear.store(1.0f, std::memory_order_relaxed);
        _gainSource.store(GainSource::Auto, std::memory_order_relaxed);
        _currentGain.store(1.0f, std::memory_order_relaxed);
    }

    _lastError.clear();

    return true;
}

bool AudioEngine::loadStream(std::string const & url, std::string const & name)
{
    stop();
    _lastError.clear();

    _currentFormat = AudioFormat::Unknown;
    _currentTrack.path = url;
    _currentTrack.format = AudioFormat::Unknown;
    _currentTrack.title = name.empty() ? std::string("Stream") : vtplayer::toNfc(name);
    _currentTrack.artist.clear();
    _currentTrack.duration = 0.0f; // live → unknown

    auto src = std::make_unique<StreamSource>();
    src->setBuffer(_streamBufferSec, _streamPrebufferSec);
    src->setDebug(_streamDebug);
    if (!src->start(url))
    {
        _lastError = src->error();
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(_audioMutex);
        _stream = std::move(src);
    }
    _isStream.store(true, std::memory_order_release);

    _framesPlayed = 0;
    _position.store(0.0f, std::memory_order_relaxed);
    _duration.store(0.0f, std::memory_order_relaxed);
    // Streams have no ReplayGain tags; use the runtime auto-gain path.
    _replayGainLinear.store(1.0f, std::memory_order_relaxed);
    _gainSource.store(GainSource::Auto, std::memory_order_relaxed);
    _currentGain.store(1.0f, std::memory_order_relaxed);
    return true;
}

bool AudioEngine::isStreamBuffering() const
{
    if (!_isStream.load(std::memory_order_acquire))
        return false;
    std::lock_guard<std::mutex> lock(_audioMutex);
    return _stream && _stream->buffering();
}

void AudioEngine::play()
{
    if (_state.load(std::memory_order_acquire) == PlayState::Playing)
    {
        return;
    }

    if (_state.load(std::memory_order_acquire) == PlayState::Paused)
    {
        _state.store(PlayState::Playing, std::memory_order_release);
        ma_device_start(_device);
        return;
    }

    // Start fresh playback
    if (!_decoder && !_isStream.load(std::memory_order_acquire))
    {
        return;
    }

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = CHANNELS;
    deviceConfig.sampleRate = SAMPLE_RATE;
    deviceConfig.dataCallback = dataCallback;
    deviceConfig.pUserData = this;

    if (ma_device_init(nullptr, &deviceConfig, _device) != MA_SUCCESS)
    {
        return;
    }

    _state.store(PlayState::Playing, std::memory_order_release);

    if (ma_device_start(_device) != MA_SUCCESS)
    {
        ma_device_uninit(_device);
        _state.store(PlayState::Stopped, std::memory_order_release);
        return;
    }
}

void AudioEngine::pause()
{
    if (_state.load(std::memory_order_acquire) != PlayState::Playing)
    {
        return;
    }

    _state.store(PlayState::Paused, std::memory_order_release);
    ma_device_stop(_device);
}

void AudioEngine::stop()
{
    auto prev = _state.exchange(PlayState::Stopped, std::memory_order_acq_rel);

    // Uninit the device first — this blocks until the audio callback
    // has fully returned, so after this point no thread touches _decoder.
    if (prev != PlayState::Stopped)
    {
        ma_device_uninit(_device);
    }

    std::lock_guard<std::mutex> lock(_audioMutex);

    if (_decoder)
    {
        _decoder.reset();
    }

    if (_stream)
    {
        _stream->stop();
        _stream.reset();
    }
    _isStream.store(false, std::memory_order_release);

    _trackEnded.store(false, std::memory_order_relaxed);
    _framesPlayed = 0;
    _position.store(0.0f, std::memory_order_relaxed);
    _currentGain.store(1.0f, std::memory_order_relaxed);
    _replayGainLinear.store(1.0f, std::memory_order_relaxed);
    _gainSource.store(GainSource::None, std::memory_order_relaxed);
}

void AudioEngine::seek(float seconds)
{
    if (_state.load(std::memory_order_acquire) == PlayState::Stopped)
    {
        return;
    }

    float dur = _duration.load(std::memory_order_relaxed);
    seconds = std::clamp(seconds, 0.0f, dur);

    std::lock_guard<std::mutex> lock(_audioMutex);

    if (_decoder)
    {
        if (_decoder->seek(static_cast<double>(seconds)))
        {
            _framesPlayed = static_cast<uint64_t>(seconds * SAMPLE_RATE);
            _position.store(seconds, std::memory_order_relaxed);
        }
    }
}

void AudioEngine::setVolume(float v)
{
    _volume.store(std::clamp(v, 0.0f, VOLUME_MAX), std::memory_order_relaxed);
}

void AudioEngine::setGainNorm(bool enabled)
{
    _gainNormEnabled.store(enabled, std::memory_order_relaxed);
    // _currentGain itself is ramped smoothly by fillBuffer, no hard reset.
}

float AudioEngine::gainNormDb() const
{
    float g = _currentGain.load(std::memory_order_relaxed);
    return 20.0f * std::log10(std::max(g, 1e-6f));
}

int AudioEngine::getSamples(float * out, int count) const
{
    return _visBuffer.readLatest(out, count);
}

void AudioEngine::dataCallback(
    ma_device * device,
    void * output,
    void const * /* input */,
    unsigned int frameCount)
{
    auto * engine = static_cast<AudioEngine *>(device->pUserData);

    if (engine->_state.load(std::memory_order_acquire) != PlayState::Playing)
    {
        std::memset(output, 0, frameCount * CHANNELS * sizeof(float));
        return;
    }

    auto * out = static_cast<float *>(output);
    engine->fillBuffer(out, frameCount);
}

void AudioEngine::fillBuffer(float * output, unsigned int frameCount)
{
    float vol = _volume.load(std::memory_order_relaxed);
    bool normOn = _gainNormEnabled.load(std::memory_order_relaxed);
    GainSource src = _gainSource.load(std::memory_order_relaxed);
    unsigned int totalSamples = frameCount * CHANNELS;

    std::lock_guard<std::mutex> lock(_audioMutex);

    unsigned int framesRead = 0;

    if (_isStream.load(std::memory_order_acquire))
    {
        unsigned int const got =
            _stream ? _stream->read(output, frameCount) : 0u;
        framesRead = got;
        if (got < frameCount)
        {
            // Underrun: emit silence for the gap. Only flag end-of-track
            // when ffmpeg has actually exited and the buffer is drained —
            // a transient buffer dip must not look like the stream ending.
            std::memset(output + got * CHANNELS, 0,
                        (frameCount - got) * CHANNELS * sizeof(float));
            if (_stream && _stream->ended())
                _trackEnded.store(true, std::memory_order_release);
        }
    }
    else
    {
        if (!_decoder)
        {
            std::memset(output, 0, totalSamples * sizeof(float));
            return;
        }

        framesRead = _decoder->read(output, frameCount);

        if (framesRead < frameCount)
        {
            // End of file — zero remaining
            std::memset(output + framesRead * CHANNELS, 0,
                        (frameCount - framesRead) * CHANNELS * sizeof(float));
            _trackEnded.store(true, std::memory_order_release);
        }
    }

    // --- Resolve target gain and smooth toward it ---
    float gainStart = _currentGain.load(std::memory_order_relaxed);
    float gainEnd = gainStart;
    if (framesRead > 0)
    {
        float desired = 1.0f;

        if (normOn && src == GainSource::ReplayGain)
        {
            desired = _replayGainLinear.load(std::memory_order_relaxed);
        }
        else if (normOn /* GainSource::Auto or fallback */)
        {
            // RMS-based estimate on pre-gain mono mixdown.
            double sumSq = 0.0;
            for (ma_uint64 i = 0; i < framesRead; ++i)
            {
                float m = (output[i * 2] + output[i * 2 + 1]) * 0.5f;
                sumSq += static_cast<double>(m) * m;
            }
            float rms = std::sqrt(static_cast<float>(sumSq / framesRead));
            if (rms > kAutoGainNoiseGate)
            {
                desired = std::clamp(kAutoGainTargetRms / rms,
                                     kGainMinLin, kGainMaxLin);
            }
            else
            {
                desired = gainStart; // hold during silence
            }
        }
        // else: normalization disabled → desired = 1.0 (ramp back to unity)

        float tauSamples = (desired < gainStart) ? kGainAttackSamples
                                                 : kGainReleaseSamples;
        float alpha = std::min(1.0f, static_cast<float>(frameCount) / tauSamples);
        gainEnd = gainStart + (desired - gainStart) * alpha;
        _currentGain.store(gainEnd, std::memory_order_relaxed);
    }

    // --- Apply combined gain with per-sample linear ramp ---
    float totalStart = gainStart * vol;
    float totalEnd = gainEnd * vol;
    float step = (framesRead > 0)
                     ? (totalEnd - totalStart) / static_cast<float>(framesRead)
                     : 0.0f;
    bool mayClip = (totalStart > 1.0f) || (totalEnd > 1.0f);
    float g = totalStart;
    if (mayClip)
    {
        for (ma_uint64 i = 0; i < framesRead; ++i)
        {
            output[i * 2]     = softClip(output[i * 2]     * g);
            output[i * 2 + 1] = softClip(output[i * 2 + 1] * g);
            g += step;
        }
    }
    else
    {
        for (ma_uint64 i = 0; i < framesRead; ++i)
        {
            output[i * 2]     *= g;
            output[i * 2 + 1] *= g;
            g += step;
        }
    }

    // Feed visualization buffer (mono mixdown, post-processing)
    float mono[1024];
    unsigned int monoCount = std::min(frameCount, 1024u);
    for (unsigned int i = 0; i < monoCount; ++i)
    {
        mono[i] = (output[i * 2] + output[i * 2 + 1]) * 0.5f;
    }
    _visBuffer.write(mono, static_cast<int>(monoCount));

    _framesPlayed += frameCount;
    _position.store(static_cast<float>(_framesPlayed) / static_cast<float>(SAMPLE_RATE),
                    std::memory_order_relaxed);
}

} // namespace vtplayer
