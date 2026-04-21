// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#define MA_LOG_LEVEL MA_LOG_LEVEL_ERROR
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio/miniaudio.h>

#include "AudioEngine.h"

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

// Auto-gain tuning (ReplayGain-style reference, runtime only — no tags written).
constexpr float kAutoGainTargetRms = 0.12589f;  ///< -18 dBFS
constexpr float kAutoGainNoiseGate = 0.003162f; ///< -50 dBFS, skip updates below
constexpr float kAutoGainMaxLin = 3.981f;       ///< +12 dB ceiling
constexpr float kAutoGainMinLin = 0.2512f;      ///< -12 dB floor
constexpr float kAutoGainAttackSamples  = 0.15f * 44100.0f; ///< 150ms (when reducing gain)
constexpr float kAutoGainReleaseSamples = 2.5f  * 44100.0f; ///< 2.5s (when raising gain)

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
    _currentTrack.title = path.stem().string();
    _currentTrack.artist.clear();
    _currentTrack.duration = 0.0f;

    _decoder = new ma_decoder;

    ma_decoder_config decoderConfig = ma_decoder_config_init(
        ma_format_f32, CHANNELS, SAMPLE_RATE);

    if (ma_decoder_init_file(path.string().c_str(), &decoderConfig, _decoder) != MA_SUCCESS)
    {
        _lastError = "Failed to open audio file";
        delete _decoder;
        _decoder = nullptr;
        return false;
    }

    ma_uint64 totalFrames = 0;
    ma_decoder_get_length_in_pcm_frames(_decoder, &totalFrames);
    _currentTrack.duration = static_cast<float>(totalFrames) / static_cast<float>(SAMPLE_RATE);
    _duration.store(_currentTrack.duration, std::memory_order_relaxed);

    _framesPlayed = 0;
    _position.store(0.0f, std::memory_order_relaxed);
    _autoGain.store(1.0f, std::memory_order_relaxed);
    _lastError.clear();

    return true;
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
    if (!_decoder)
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
        ma_decoder_uninit(_decoder);
        delete _decoder;
        _decoder = nullptr;
    }

    _trackEnded.store(false, std::memory_order_relaxed);
    _framesPlayed = 0;
    _position.store(0.0f, std::memory_order_relaxed);
    _autoGain.store(1.0f, std::memory_order_relaxed);
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
        ma_uint64 frame = static_cast<ma_uint64>(seconds * SAMPLE_RATE);
        ma_decoder_seek_to_pcm_frame(_decoder, frame);
        _framesPlayed = frame;
        _position.store(seconds, std::memory_order_relaxed);
    }
}

void AudioEngine::setVolume(float v)
{
    _volume.store(std::clamp(v, 0.0f, VOLUME_MAX), std::memory_order_relaxed);
}

void AudioEngine::setAutoGain(bool enabled)
{
    _autoGainEnabled.store(enabled, std::memory_order_relaxed);
    // _autoGain itself is ramped smoothly by fillBuffer, no hard reset.
}

float AudioEngine::autoGainDb() const
{
    float g = _autoGain.load(std::memory_order_relaxed);
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
    bool agOn = _autoGainEnabled.load(std::memory_order_relaxed);
    unsigned int totalSamples = frameCount * CHANNELS;

    std::lock_guard<std::mutex> lock(_audioMutex);

    if (!_decoder)
    {
        std::memset(output, 0, totalSamples * sizeof(float));
        return;
    }

    ma_uint64 framesRead = 0;
    ma_decoder_read_pcm_frames(_decoder, output, frameCount, &framesRead);

    if (framesRead < frameCount)
    {
        // End of file — zero remaining
        std::memset(output + framesRead * CHANNELS, 0,
                    (frameCount - framesRead) * CHANNELS * sizeof(float));
        _trackEnded.store(true, std::memory_order_release);
    }

    // --- Auto-gain analysis (on pre-gain mono mixdown) ---
    float autoStart = _autoGain.load(std::memory_order_relaxed);
    float autoEnd = autoStart;
    if (framesRead > 0)
    {
        if (agOn)
        {
            double sumSq = 0.0;
            for (ma_uint64 i = 0; i < framesRead; ++i)
            {
                float m = (output[i * 2] + output[i * 2 + 1]) * 0.5f;
                sumSq += static_cast<double>(m) * m;
            }
            float rms = std::sqrt(static_cast<float>(sumSq / framesRead));
            float desired = autoStart;
            if (rms > kAutoGainNoiseGate)
            {
                desired = std::clamp(kAutoGainTargetRms / rms,
                                     kAutoGainMinLin, kAutoGainMaxLin);
            }
            float tauSamples = (desired < autoStart)
                                   ? kAutoGainAttackSamples
                                   : kAutoGainReleaseSamples;
            float alpha = std::min(1.0f, static_cast<float>(frameCount) / tauSamples);
            autoEnd = autoStart + (desired - autoStart) * alpha;
        }
        else
        {
            // Smoothly ramp back to unity when disabled, avoiding a click on toggle.
            float alpha = std::min(1.0f, static_cast<float>(frameCount) / kAutoGainReleaseSamples);
            autoEnd = autoStart + (1.0f - autoStart) * alpha;
        }
        _autoGain.store(autoEnd, std::memory_order_relaxed);
    }

    // --- Apply combined gain with per-sample linear ramp ---
    float totalStart = autoStart * vol;
    float totalEnd = autoEnd * vol;
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
