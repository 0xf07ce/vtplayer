// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "RingBuffer.h"
#include "TrackInfo.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

// Forward declarations
struct ma_device;
struct ma_decoder;

namespace vtplayer
{
class StreamSource;
}

namespace vtplayer
{

enum class PlayState
{
    Stopped,
    Playing,
    Paused,
};

/// Source of the current gain-normalization value applied to playback.
enum class GainSource
{
    None,        ///< No gain normalization (or no track loaded).
    Auto,        ///< RMS-based runtime estimate.
    ReplayGain,  ///< Static value from REPLAYGAIN_TRACK_GAIN tag.
};

class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    bool init();
    void shutdown();

    bool load(std::filesystem::path const & path);

    /// Load a network stream (internet radio). Decoded by an external ffmpeg
    /// process; `name` is shown as the track title. Live = unknown duration
    /// (duration() stays 0, so the transport bar shows LIVE). Returns false
    /// with lastError() set if ffmpeg is unavailable or the spawn failed.
    bool loadStream(std::string const & url, std::string const & name);

    /// True while the current source is a network stream.
    bool isStream() const { return _isStream.load(std::memory_order_acquire); }

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

    /// Master switch for gain normalization. When enabled, the engine uses the
    /// track's REPLAYGAIN_TRACK_GAIN tag if present, otherwise falls back to a
    /// runtime RMS-based estimate targeted at -18 dBFS.
    void setGainNorm(bool enabled);
    bool gainNormEnabled() const { return _gainNormEnabled.load(std::memory_order_relaxed); }
    /// Currently applied gain in dB (smoothed; reflects ramp).
    float gainNormDb() const;
    /// Which source is driving the current track's gain target.
    GainSource gainSource() const { return _gainSource.load(std::memory_order_relaxed); }

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

    std::unique_ptr<StreamSource> _stream;          ///< set when streaming
    std::atomic<bool> _isStream{false};

    std::atomic<PlayState> _state{PlayState::Stopped};
    std::atomic<bool> _trackEnded{false}; ///< set by callback, polled by UI
    std::atomic<float> _position{0.0f};
    std::atomic<float> _duration{0.0f};
    std::atomic<float> _volume{1.0f};

    std::atomic<bool> _gainNormEnabled{false};
    std::atomic<float> _currentGain{1.0f};       ///< smoothed applied gain (linear)
    std::atomic<float> _replayGainLinear{1.0f};  ///< static target from RG tag (1.0 if absent)
    std::atomic<GainSource> _gainSource{GainSource::None};

    std::string _lastError;
    mutable std::mutex _audioMutex; ///< protects _decoder, seek
    uint64_t _framesPlayed = 0;
    TrackInfo _currentTrack;
    AudioFormat _currentFormat = AudioFormat::Unknown;

    RingBuffer<float, VIS_BUFFER_SIZE> _visBuffer;
};

} // namespace vtplayer
