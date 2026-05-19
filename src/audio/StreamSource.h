// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vtplayer
{

/// Network audio stream decoded by an external `ffmpeg` subprocess.
///
/// ffmpeg handles all of the hard parts (HTTP, HLS playlist polling, token
/// rotation, AAC/MP3/Opus, reconnection) and emits raw interleaved stereo
/// `float32` PCM at `kSampleRate` on stdout. A reader thread drains that pipe
/// into a bounded ring buffer; the audio callback pulls from it via read().
/// The buffer drops the oldest samples on overflow so playback stays close to
/// the live edge instead of drifting behind after an underrun/pause.
///
/// To mask network jitter, read() holds playback (returns 0 → caller emits
/// silence) until a prebuffer threshold of audio has accumulated. The same
/// gate re-arms on an underrun, so a momentary buffer dip rebuffers cleanly
/// instead of stuttering. buffering() reports that gated state for the UI.
///
/// When the ring is full the reader blocks (backpressure) rather than
/// discarding the oldest samples: this trades live-edge latency for a deep,
/// stable cushion, which is the right call for radio. Buffer depth and the
/// prebuffer threshold are configurable via setBuffer().
class StreamSource
{
public:
    static constexpr int kSampleRate = 44100;
    static constexpr int kChannels   = 2;

    StreamSource() = default;
    ~StreamSource();

    StreamSource(StreamSource const &)            = delete;
    StreamSource & operator=(StreamSource const &) = delete;

    /// Set ring-buffer depth and the prebuffer/rebuffer threshold (seconds).
    /// Must be called before start(); clamped to sane minimums. prebuffer is
    /// additionally clamped below the ring depth so the gate can be reached.
    void setBuffer(double bufferSeconds, double prebufferSeconds);

    /// Debug mode: leave ffmpeg's stderr inherited so its diagnostics print
    /// to the terminal (default). When false, ffmpeg's stderr is redirected
    /// to /dev/null so transient HTTP/reconnect noise never reaches the TUI.
    /// Must be called before start().
    void setDebug(bool debug) { _debug = debug; }

    /// Spawn ffmpeg for `url`. Returns false (with error()) if ffmpeg is not
    /// on PATH or the process/pipe could not be created.
    bool start(std::string const & url);

    /// Kill ffmpeg and join the reader thread. Safe to call repeatedly.
    void stop();

    /// Pull up to `frames` stereo frames into `out` (out holds
    /// frames*kChannels floats). Returns the number of frames actually
    /// written; the caller zero-fills the remainder on underrun.
    unsigned int read(float * out, unsigned int frames);

    /// ffmpeg has exited (or the pipe closed) and the buffer is drained.
    bool ended() const;

    /// Playback is gated while the prebuffer fills (initial start or after
    /// an underrun). True until enough audio has accumulated to resume.
    bool buffering() const { return _buffering.load(std::memory_order_acquire); }

    std::string const & error() const { return _error; }

private:
    void readerLoop();

    std::string _error;

    int       _readFd = -1;
    int       _pid    = -1;
    std::thread _reader;
    std::atomic<bool> _stopFlag{false};
    std::atomic<bool> _eofFlag{false};   ///< pipe closed / ffmpeg gone
    std::atomic<bool> _buffering{true};  ///< prebuffer gate (start / underrun)
    bool _debug = false;                 ///< keep ffmpeg stderr on terminal

    // Buffer sizing (seconds). Defaults favour stability over latency.
    double _bufferSeconds    = 20.0;
    double _prebufferSeconds = 5.0;
    std::size_t _prebufferSamples = 0;   ///< derived in start()

    // Bounded ring buffer of interleaved float samples.
    mutable std::mutex      _mtx;
    std::condition_variable _cv;         ///< reader backpressure / wakeups
    std::vector<float>      _ring;
    std::size_t             _head = 0;   ///< read cursor
    std::size_t             _size = 0;   ///< valid samples in ring
};

} // namespace vtplayer
