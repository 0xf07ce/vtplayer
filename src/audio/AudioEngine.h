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

namespace vtplayer
{
class Decoder;
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

    /// Unified track-load entry point. When `track.isStream()` opens the URL
    /// via StreamSource (live, unknown duration → transport shows LIVE);
    /// otherwise opens the local file at `track.path` via Decoder. Returns
    /// false with lastError() set on failure.
    bool load(TrackInfo const & track);

    /// Convenience wrapper: build a minimal TrackInfo from a path and forward
    /// to load(TrackInfo). Used when the caller does not have a TrackInfo
    /// at hand (e.g. drag-and-drop, startup --path argument).
    bool load(std::filesystem::path const & path);

    /// True while the current source is a network stream.
    bool isStream() const { return _isStream.load(std::memory_order_acquire); }

    /// True while a live stream is (re)buffering and emitting silence
    /// (initial prebuffer or recovering from an underrun).
    bool isStreamBuffering() const;

    /// Ring-buffer depth / prebuffer threshold (seconds) for the next
    /// loadStream(). Sourced from config; takes effect on the next stream.
    void setStreamBuffer(float bufferSeconds, float prebufferSeconds)
    {
        _streamBufferSec    = bufferSeconds;
        _streamPrebufferSec = prebufferSeconds;
    }

    /// Forwarded to StreamSource: keep ffmpeg's stderr on the terminal
    /// (debug) vs. redirect it to /dev/null (default). Next stream only.
    void setStreamDebug(bool debug) { _streamDebug = debug; }

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

    /// Refresh the stored TrackInfo (used after a tag edit so the
    /// transport bar / visualizer see the new metadata without reloading
    /// the decoder). The path must match the currently-loaded track; the
    /// engine does not look at format/streamUrl from this call.
    void updateCurrentTrackMeta(TrackInfo const & info) { _currentTrack = info; }

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
    std::unique_ptr<Decoder> _decoder;              ///< libav decoder for files

    std::unique_ptr<StreamSource> _stream;          ///< set when streaming
    std::atomic<bool> _isStream{false};
    float _streamBufferSec    = 20.0f;              ///< config: ring depth
    float _streamPrebufferSec = 5.0f;               ///< config: prebuffer
    bool  _streamDebug        = false;              ///< keep ffmpeg stderr

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
