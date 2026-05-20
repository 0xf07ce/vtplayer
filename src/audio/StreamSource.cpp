// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "StreamSource.h"

#include "Decoder.h"

extern "C" {
#include <libavutil/log.h>
}

#include <algorithm>

namespace vtplayer
{

namespace
{

constexpr std::size_t kSamplesPerSec =
    static_cast<std::size_t>(StreamSource::kSampleRate)
    * StreamSource::kChannels;

} // namespace

StreamSource::StreamSource() = default;

StreamSource::~StreamSource()
{
    stop();
}

void StreamSource::setBuffer(double bufferSeconds, double prebufferSeconds)
{
    // Floors keep the math sane; prebuffer must stay below the ring depth
    // (with a margin) so the gate is actually reachable before the reader
    // applies backpressure.
    _bufferSeconds    = std::max(2.0, bufferSeconds);
    _prebufferSeconds = std::max(0.5, prebufferSeconds);
    _prebufferSeconds = std::min(_prebufferSeconds, _bufferSeconds * 0.8);
}

bool StreamSource::start(std::string const & url)
{
    stop();
    _error.clear();
    _stopFlag.store(false);
    _eofFlag.store(false);
    _buffering.store(true); // prebuffer before the first sample plays
    _head = _size = 0;

    std::size_t const ringSamples = static_cast<std::size_t>(
        _bufferSeconds * static_cast<double>(kSamplesPerSec));
    _prebufferSamples = static_cast<std::size_t>(
        _prebufferSeconds * static_cast<double>(kSamplesPerSec));
    _ring.assign(ringSamples, 0.0f);

    // libav's log level is process-global; --debug raises it so live-radio
    // diagnostics (reconnect attempts, codec quirks) print to the terminal.
    av_log_set_level(_debug ? AV_LOG_VERBOSE : AV_LOG_ERROR);

    auto dec = std::make_unique<Decoder>();
    // Arm libav's interrupt callback against our stop flag so a stuck
    // network read in the reader thread can be aborted promptly by stop().
    dec->setInterrupt(&_stopFlag);
    if (!dec->open(url, true))
    {
        _error = dec->error();
        return false;
    }

    _decoder = std::move(dec);
    _reader  = std::thread(&StreamSource::readerLoop, this);
    return true;
}

void StreamSource::stop()
{
    _stopFlag.store(true);
    _cv.notify_all(); // wake a reader parked on backpressure

    // Critical ordering: we MUST join the reader thread BEFORE tearing down
    // the decoder. The reader may be deep inside libav (av_read_frame,
    // socket read, codec receive) holding pointers into the format/codec
    // contexts; freeing them now would be a use-after-free. The interrupt
    // callback armed in start() makes libav return from its blocking call
    // promptly once _stopFlag is set, so the join completes in tens of ms.
    if (_reader.joinable())
        _reader.join();

    // Reader is gone — no other thread touches _decoder. Safe to tear down.
    _decoder.reset();

    _buffering.store(true);
    std::lock_guard<std::mutex> lk(_mtx);
    _ring.clear();
    _head = _size = 0;
}

void StreamSource::readerLoop()
{
    constexpr unsigned int kChunkFrames = 1024;
    std::vector<float> chunk(static_cast<std::size_t>(kChunkFrames) * kChannels);
    while (!_stopFlag.load())
    {
        if (!_decoder)
        {
            _eofFlag.store(true);
            return;
        }
        unsigned int const gotFrames = _decoder->read(chunk.data(), kChunkFrames);
        if (gotFrames == 0)
        {
            // EOF or hard error on the decoder side — either way, no more
            // samples will arrive.
            _eofFlag.store(true);
            return;
        }
        std::size_t const got =
            static_cast<std::size_t>(gotFrames) * kChannels;

        std::unique_lock<std::mutex> lk(_mtx);
        std::size_t const cap = _ring.size();
        if (cap == 0)
            continue;
        for (std::size_t i = 0; i < got; ++i)
        {
            if (_size == cap)
            {
                // Ring full: apply backpressure rather than discarding the
                // cushion. Park until the consumer frees space (or we stop).
                _cv.wait(lk, [&] {
                    return _stopFlag.load() || _size < cap;
                });
                if (_stopFlag.load())
                    return;
            }
            std::size_t const tail = (_head + _size) % cap;
            _ring[tail] = chunk[i];
            ++_size;
        }
    }
}

unsigned int StreamSource::read(float * out, unsigned int frames)
{
    std::size_t const want = static_cast<std::size_t>(frames) * kChannels;
    std::unique_lock<std::mutex> lk(_mtx);
    std::size_t const cap = _ring.size();
    bool const eof = _eofFlag.load(std::memory_order_acquire);

    if (_buffering.load(std::memory_order_relaxed))
    {
        // Gate playback (caller emits silence) until the prebuffer fills, so
        // a fresh start or post-underrun resume does not immediately stutter.
        // EOF lifts the gate so the final tail still drains.
        if (_size < _prebufferSamples && !eof)
            return 0;
        _buffering.store(false, std::memory_order_release);
    }
    else if (_size < want && !eof)
    {
        // Underrun: not enough for a full callback and more is still coming.
        // Re-arm the prebuffer gate instead of feeding a torn partial chunk.
        _buffering.store(true, std::memory_order_release);
        return 0;
    }

    std::size_t const give = std::min(want, _size);
    for (std::size_t i = 0; i < give; ++i)
        out[i] = _ring[(_head + i) % cap];
    if (cap > 0)
        _head = (_head + give) % cap;
    _size -= give;
    if (give > 0)
    {
        lk.unlock();
        _cv.notify_one(); // space freed → release a backpressured reader
    }
    return static_cast<unsigned int>(give / kChannels);
}

bool StreamSource::ended() const
{
    if (!_eofFlag.load())
        return false;
    std::lock_guard<std::mutex> lk(_mtx);
    return _size == 0;
}

} // namespace vtplayer
