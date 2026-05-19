// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <atomic>
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
class StreamSource
{
public:
    static constexpr int kSampleRate = 44100;
    static constexpr int kChannels   = 2;

    StreamSource() = default;
    ~StreamSource();

    StreamSource(StreamSource const &)            = delete;
    StreamSource & operator=(StreamSource const &) = delete;

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

    std::string const & error() const { return _error; }

private:
    void readerLoop();

    std::string _error;

    int       _readFd = -1;
    int       _pid    = -1;
    std::thread _reader;
    std::atomic<bool> _stopFlag{false};
    std::atomic<bool> _eofFlag{false};   ///< pipe closed / ffmpeg gone

    // Bounded ring buffer of interleaved float samples.
    mutable std::mutex   _mtx;
    std::vector<float>   _ring;
    std::size_t          _head = 0;      ///< read cursor
    std::size_t          _size = 0;      ///< valid samples in ring
};

} // namespace vtplayer
