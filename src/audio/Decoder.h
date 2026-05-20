// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;
struct AVPacket;
struct AVFrame;
struct SwrContext;

namespace vtplayer
{

/// Unified audio decoder built on libavformat + libavcodec + libswresample.
///
/// `Decoder` is the single backend behind every audio source in vtplayer —
/// local files (mp3/wav/ogg/flac/m4a/aac/opus/wma/…) and network streams
/// (HTTP/HLS internet radio). Output is always normalised to interleaved
/// float32, stereo, 44.1 kHz so the rest of the audio pipeline does not have
/// to care what the source was.
///
/// `miniaudio` is intentionally not used for decoding — it now only drives
/// `ma_device` for cross-platform audio output (see `AudioEngine`).
class Decoder
{
public:
    static constexpr int kSampleRate = 44100;
    static constexpr int kChannels   = 2;

    Decoder() = default;
    ~Decoder();

    Decoder(Decoder const &)             = delete;
    Decoder & operator=(Decoder const &) = delete;

    /// Arm an interrupt source for libav blocking I/O (network reads, slow
    /// disk). When the pointed-to flag becomes true mid-call, libav returns
    /// promptly so the caller can join its reader thread and tear down
    /// contexts safely. Must be called BEFORE open(); the pointer must
    /// remain valid for the lifetime of the Decoder.
    void setInterrupt(std::atomic<bool> const * flag) { _interruptFlag = flag; }

    /// Open a file path or URL. URLs get HTTP reconnect options matching the
    /// previous external-ffmpeg invocation. Returns false with error() set on
    /// failure (no audio stream, unsupported codec, network failure, …).
    bool open(std::string const & source, bool isUrl);

    /// Release all libav state. Safe to call multiple times.
    void close();

    /// Pull up to `frames` stereo frames into `out` (size frames*kChannels
    /// floats). Returns the number of frames actually written. Returns 0 on
    /// EOF or error; check eof() / error() to distinguish.
    unsigned int read(float * out, unsigned int frames);

    /// Seek to `seconds` (file sources only). Returns false on streams or on
    /// libav errors.
    bool seek(double seconds);

    /// Total duration in seconds. 0 for live streams or when libav reports no
    /// known duration.
    double duration() const;

    /// True if seek() can succeed for this source (false for live streams).
    bool isSeekable() const { return _seekable; }

    /// End-of-stream reached on the last read().
    bool eof() const { return _eof; }

    std::string const & error() const { return _error; }

private:
    bool decodePacketLoop();
    void freeContexts();
    static int interruptCb(void * opaque);

    std::atomic<bool> const * _interruptFlag = nullptr;

    AVFormatContext * _fmt   = nullptr;
    AVCodecContext  * _codec = nullptr;
    SwrContext      * _swr   = nullptr;
    AVPacket        * _pkt   = nullptr;
    AVFrame         * _frame = nullptr;
    int               _audioStreamIndex = -1;

    /// Interleaved f32 samples already resampled but not yet handed to read().
    std::vector<float> _residual;
    std::size_t        _residualHead = 0;

    double _duration = 0.0;
    bool   _seekable = false;
    bool   _eof      = false;
    std::string _error;
};

} // namespace vtplayer
