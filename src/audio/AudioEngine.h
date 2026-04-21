// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "RingBuffer.h"
#include "TrackInfo.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

// Forward declarations
struct ma_device;
struct ma_decoder;

namespace vtplayer
{

enum class PlayState
{
    Stopped,
    Playing,
    Paused,
};

class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    bool init();
    void shutdown();

    bool load(std::filesystem::path const & path);
    void play();
    void pause();
    void stop();
    void seek(float seconds);

    PlayState state() const { return _state.load(std::memory_order_acquire); }
    std::string const & lastError() const { return _lastError; }
    bool hasTrackEnded() { return _trackEnded.exchange(false, std::memory_order_relaxed); }
    float position() const { return _position.load(std::memory_order_relaxed); }
    float duration() const { return _duration.load(std::memory_order_relaxed); }
    float volume() const { return _volume.load(std::memory_order_relaxed); }
    void setVolume(float v);

    /// Runtime auto-gain: RMS-based loudness normalization toward -18 dBFS.
    /// Disabled by default; no effect on file tags.
    void setAutoGain(bool enabled);
    bool autoGainEnabled() const { return _autoGainEnabled.load(std::memory_order_relaxed); }
    /// Current applied auto-gain in dB (for UI display).
    float autoGainDb() const;

    TrackInfo const & currentTrack() const { return _currentTrack; }

    /// Read latest samples for visualization (called from UI thread)
    int getSamples(float * out, int count) const;

    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int CHANNELS = 2;
    static constexpr int VIS_BUFFER_SIZE = 4096;
    static constexpr float VOLUME_MAX = 2.0f; ///< max amplification (soft-clipped above 1.0)

private:
    /// miniaudio data callback (runs on audio thread)
    static void dataCallback(ma_device * device, void * output, void const * input, unsigned int frameCount);

    /// Fill output buffer from decoder
    void fillBuffer(float * output, unsigned int frameCount);

    ma_device * _device = nullptr;
    ma_decoder * _decoder = nullptr;

    std::atomic<PlayState> _state{PlayState::Stopped};
    std::atomic<bool> _trackEnded{false}; ///< set by callback, polled by UI
    std::atomic<float> _position{0.0f};
    std::atomic<float> _duration{0.0f};
    std::atomic<float> _volume{1.0f};
    std::atomic<bool> _autoGainEnabled{false};
    std::atomic<float> _autoGain{1.0f}; ///< current smoothed auto-gain (linear)

    std::string _lastError;
    mutable std::mutex _audioMutex; ///< protects _decoder, seek
    uint64_t _framesPlayed = 0;
    TrackInfo _currentTrack;
    AudioFormat _currentFormat = AudioFormat::Unknown;

    RingBuffer<float, VIS_BUFFER_SIZE> _visBuffer;
};

} // namespace vtplayer
