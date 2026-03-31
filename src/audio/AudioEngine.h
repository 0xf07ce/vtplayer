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
struct Music_Emu;

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

    TrackInfo const & currentTrack() const { return _currentTrack; }

    /// Read latest samples for visualization (called from UI thread)
    int getSamples(float * out, int count) const;

    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int CHANNELS = 2;
    static constexpr int VIS_BUFFER_SIZE = 4096;

private:
    /// miniaudio data callback (runs on audio thread)
    static void dataCallback(ma_device * device, void * output, void const * input, unsigned int frameCount);

    /// Fill output buffer from decoder or GME
    void fillBuffer(float * output, unsigned int frameCount);

    ma_device * _device = nullptr;
    ma_decoder * _decoder = nullptr;
    Music_Emu * _emu = nullptr;

    std::atomic<PlayState> _state{PlayState::Stopped};
    std::atomic<bool> _trackEnded{false}; ///< set by callback, polled by UI
    std::atomic<float> _position{0.0f};
    std::atomic<float> _duration{0.0f};
    std::atomic<float> _volume{0.8f};

    std::string _lastError;
    mutable std::mutex _audioMutex; ///< protects _decoder, _emu, seek
    uint64_t _framesPlayed = 0;
    TrackInfo _currentTrack;
    AudioFormat _currentFormat = AudioFormat::Unknown;

    RingBuffer<float, VIS_BUFFER_SIZE> _visBuffer;
};

} // namespace vtplayer
