// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "Decoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cstring>

namespace vtplayer
{

namespace
{

std::string avErr(int code)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buf, sizeof(buf));
    return std::string(buf);
}

} // namespace

Decoder::~Decoder()
{
    close();
}

void Decoder::freeContexts()
{
    if (_swr)
    {
        swr_free(&_swr);
        _swr = nullptr;
    }
    if (_codec)
    {
        avcodec_free_context(&_codec);
        _codec = nullptr;
    }
    if (_fmt)
    {
        avformat_close_input(&_fmt);
        _fmt = nullptr;
    }
    if (_frame)
    {
        av_frame_free(&_frame);
        _frame = nullptr;
    }
    if (_pkt)
    {
        av_packet_free(&_pkt);
        _pkt = nullptr;
    }
}

void Decoder::close()
{
    freeContexts();
    _audioStreamIndex = -1;
    _residual.clear();
    _residualHead = 0;
    _duration = 0.0;
    _seekable = false;
    _eof = false;
    _error.clear();
}

int Decoder::interruptCb(void * opaque)
{
    auto * self = static_cast<Decoder *>(opaque);
    if (self && self->_interruptFlag &&
        self->_interruptFlag->load(std::memory_order_acquire))
        return 1; // tell libav to abandon the current blocking call
    return 0;
}

bool Decoder::open(std::string const & source, bool isUrl)
{
    close();

    // Pre-allocate the format context so the interrupt callback is armed
    // before avformat_open_input attempts any (potentially blocking) I/O.
    // On open failure libav frees _fmt and sets it to NULL.
    _fmt = avformat_alloc_context();
    if (!_fmt)
    {
        _error = "alloc format ctx failed";
        return false;
    }
    _fmt->interrupt_callback.callback = &Decoder::interruptCb;
    _fmt->interrupt_callback.opaque   = this;

    AVDictionary * opts = nullptr;
    if (isUrl)
    {
        // Match the previous external-ffmpeg invocation: survive transient
        // HTTP drops, identify ourselves as something most CDNs accept.
        av_dict_set(&opts, "reconnect",            "1", 0);
        av_dict_set(&opts, "reconnect_streamed",   "1", 0);
        av_dict_set(&opts, "reconnect_delay_max",  "5", 0);
        av_dict_set(&opts, "user_agent",           "vtplayer/0.8", 0);
        // Tighten the connect timeout so a dead host fails fast (5 s).
        av_dict_set(&opts, "rw_timeout",           "5000000", 0);
    }

    int rc = avformat_open_input(&_fmt, source.c_str(), nullptr, &opts);
    av_dict_free(&opts);
    if (rc < 0)
    {
        _error = "open failed: " + avErr(rc);
        return false;
    }

    rc = avformat_find_stream_info(_fmt, nullptr);
    if (rc < 0)
    {
        _error = "stream info failed: " + avErr(rc);
        freeContexts();
        return false;
    }

    AVCodec const * codec = nullptr;
    _audioStreamIndex = av_find_best_stream(_fmt, AVMEDIA_TYPE_AUDIO, -1, -1,
                                            &codec, 0);
    if (_audioStreamIndex < 0 || !codec)
    {
        _error = "no audio stream";
        freeContexts();
        return false;
    }

    _codec = avcodec_alloc_context3(codec);
    if (!_codec)
    {
        _error = "alloc codec ctx failed";
        freeContexts();
        return false;
    }

    rc = avcodec_parameters_to_context(
        _codec, _fmt->streams[_audioStreamIndex]->codecpar);
    if (rc < 0)
    {
        _error = "params copy failed: " + avErr(rc);
        freeContexts();
        return false;
    }

    rc = avcodec_open2(_codec, codec, nullptr);
    if (rc < 0)
    {
        _error = "codec open failed: " + avErr(rc);
        freeContexts();
        return false;
    }

    // Set up the resampler: arbitrary input → f32 / stereo / 44.1 kHz.
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, kChannels);

    // If the decoder produced no explicit input channel layout (some raw
    // streams do this), fall back to a default for the channel count.
    AVChannelLayout inLayout;
    av_channel_layout_copy(&inLayout, &_codec->ch_layout);
    if (inLayout.order == AV_CHANNEL_ORDER_UNSPEC && inLayout.nb_channels > 0)
    {
        av_channel_layout_uninit(&inLayout);
        av_channel_layout_default(&inLayout, _codec->ch_layout.nb_channels);
    }

    rc = swr_alloc_set_opts2(&_swr,
                             &outLayout, AV_SAMPLE_FMT_FLT, kSampleRate,
                             &inLayout, _codec->sample_fmt, _codec->sample_rate,
                             0, nullptr);
    av_channel_layout_uninit(&outLayout);
    av_channel_layout_uninit(&inLayout);

    if (rc < 0 || !_swr)
    {
        _error = "swr alloc failed: " + avErr(rc);
        freeContexts();
        return false;
    }

    rc = swr_init(_swr);
    if (rc < 0)
    {
        _error = "swr init failed: " + avErr(rc);
        freeContexts();
        return false;
    }

    _pkt   = av_packet_alloc();
    _frame = av_frame_alloc();
    if (!_pkt || !_frame)
    {
        _error = "alloc packet/frame failed";
        freeContexts();
        return false;
    }

    if (_fmt->duration > 0 && _fmt->duration != AV_NOPTS_VALUE)
        _duration = static_cast<double>(_fmt->duration) / AV_TIME_BASE;
    else
        _duration = 0.0;

    // Heuristic: a finite duration generally implies a seekable container.
    // Live HTTP/HLS sources report duration = 0 (or AV_NOPTS_VALUE).
    _seekable = !isUrl && _duration > 0.0;

    _eof = false;
    _error.clear();
    return true;
}

bool Decoder::decodePacketLoop()
{
    // Pull one decoded frame, resample it into _residual, return true on
    // success; on EOF set _eof and return false; on hard error set _error.
    while (true)
    {
        int rc = avcodec_receive_frame(_codec, _frame);
        if (rc == 0)
        {
            // Resample _frame into a temporary, then append to _residual.
            int const outMax = static_cast<int>(
                av_rescale_rnd(swr_get_delay(_swr, _codec->sample_rate)
                                   + _frame->nb_samples,
                               kSampleRate, _codec->sample_rate, AV_ROUND_UP));
            std::vector<float> tmp(static_cast<std::size_t>(outMax) * kChannels);
            uint8_t * outPtr = reinterpret_cast<uint8_t *>(tmp.data());
            int const outFrames = swr_convert(
                _swr, &outPtr, outMax,
                const_cast<uint8_t const **>(_frame->data),
                _frame->nb_samples);
            av_frame_unref(_frame);
            if (outFrames < 0)
            {
                _error = "swr convert failed: " + avErr(outFrames);
                return false;
            }
            // Compact leftover then append.
            if (_residualHead > 0)
            {
                _residual.erase(_residual.begin(),
                                _residual.begin() + _residualHead);
                _residualHead = 0;
            }
            _residual.insert(_residual.end(), tmp.begin(),
                             tmp.begin() + outFrames * kChannels);
            return true;
        }
        if (rc == AVERROR(EAGAIN))
        {
            // Feed the codec another packet.
            int prc = av_read_frame(_fmt, _pkt);
            if (prc == AVERROR_EOF)
            {
                // Flush the codec on EOF.
                avcodec_send_packet(_codec, nullptr);
                rc = avcodec_receive_frame(_codec, _frame);
                if (rc == 0)
                {
                    int const outMax = static_cast<int>(
                        av_rescale_rnd(swr_get_delay(_swr,
                                                     _codec->sample_rate)
                                           + _frame->nb_samples,
                                       kSampleRate, _codec->sample_rate,
                                       AV_ROUND_UP));
                    std::vector<float> tmp(static_cast<std::size_t>(outMax)
                                           * kChannels);
                    uint8_t * outPtr =
                        reinterpret_cast<uint8_t *>(tmp.data());
                    int const outFrames = swr_convert(
                        _swr, &outPtr, outMax,
                        const_cast<uint8_t const **>(_frame->data),
                        _frame->nb_samples);
                    av_frame_unref(_frame);
                    if (outFrames > 0)
                    {
                        if (_residualHead > 0)
                        {
                            _residual.erase(_residual.begin(),
                                            _residual.begin() + _residualHead);
                            _residualHead = 0;
                        }
                        _residual.insert(
                            _residual.end(), tmp.begin(),
                            tmp.begin() + outFrames * kChannels);
                        return true;
                    }
                }
                _eof = true;
                return false;
            }
            if (prc < 0)
            {
                _error = "read frame failed: " + avErr(prc);
                return false;
            }
            if (_pkt->stream_index != _audioStreamIndex)
            {
                av_packet_unref(_pkt);
                continue;
            }
            int srcRc = avcodec_send_packet(_codec, _pkt);
            av_packet_unref(_pkt);
            if (srcRc < 0 && srcRc != AVERROR(EAGAIN))
            {
                _error = "send packet failed: " + avErr(srcRc);
                return false;
            }
            continue;
        }
        if (rc == AVERROR_EOF)
        {
            _eof = true;
            return false;
        }
        _error = "receive frame failed: " + avErr(rc);
        return false;
    }
}

unsigned int Decoder::read(float * out, unsigned int frames)
{
    if (!_codec || frames == 0)
        return 0;

    std::size_t const want = static_cast<std::size_t>(frames) * kChannels;
    std::size_t filled = 0;

    while (filled < want)
    {
        std::size_t const avail = (_residual.size() - _residualHead);
        if (avail > 0)
        {
            std::size_t const take = std::min(avail, want - filled);
            std::memcpy(out + filled, _residual.data() + _residualHead,
                        take * sizeof(float));
            _residualHead += take;
            filled += take;
            if (_residualHead == _residual.size())
            {
                _residual.clear();
                _residualHead = 0;
            }
            continue;
        }
        if (!decodePacketLoop())
            break;
    }

    return static_cast<unsigned int>(filled / kChannels);
}

bool Decoder::seek(double seconds)
{
    if (!_seekable || !_fmt || !_codec)
        return false;

    int64_t const ts = static_cast<int64_t>(seconds * AV_TIME_BASE);
    int rc = avformat_seek_file(_fmt, -1, INT64_MIN, ts, ts,
                                AVSEEK_FLAG_BACKWARD);
    if (rc < 0)
    {
        _error = "seek failed: " + avErr(rc);
        return false;
    }
    avcodec_flush_buffers(_codec);
    _residual.clear();
    _residualHead = 0;
    _eof = false;
    return true;
}

double Decoder::duration() const
{
    return _duration;
}

} // namespace vtplayer
