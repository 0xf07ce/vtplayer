// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#define MA_LOG_LEVEL MA_LOG_LEVEL_ERROR
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "AudioEngine.h"

#include <gme/gme.h>

#include <algorithm>
#include <cstring>

namespace vtamp
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

    if (_currentFormat == AudioFormat::Gme)
    {
        // Use game-music-emu
        gme_err_t err = gme_open_file(path.string().c_str(), &_emu, SAMPLE_RATE);
        if (err)
        {
            _lastError = std::string("GME: ") + err;
            _emu = nullptr;
            return false;
        }
        if (!_emu)
        {
            _lastError = "GME: failed to create emulator";
            return false;
        }

        if (gme_track_count(_emu) < 1)
        {
            _lastError = "GME: no tracks in file";
            gme_delete(_emu);
            _emu = nullptr;
            return false;
        }

        // Read metadata
        gme_info_t * info = nullptr;
        if (!gme_track_info(_emu, &info, 0))
        {
            if (info->length > 0)
            {
                _currentTrack.duration = static_cast<float>(info->length) / 1000.0f;
            }
            else if (info->intro_length + info->loop_length > 0)
            {
                _currentTrack.duration = static_cast<float>(info->intro_length + info->loop_length * 2) / 1000.0f;
            }
            else
            {
                _currentTrack.duration = 180.0f; // default 3 min for VGM
            }

            if (info->song && info->song[0])
            {
                _currentTrack.title = info->song;
            }
            if (info->author && info->author[0])
            {
                _currentTrack.artist = info->author;
            }
            if (info->system && info->system[0])
            {
                _lastError = std::string("System: ") + info->system;
            }
            gme_free_info(info);
        }

        err = gme_start_track(_emu, 0);
        if (err)
        {
            _lastError = std::string("GME start: ") + err;
            gme_delete(_emu);
            _emu = nullptr;
            return false;
        }

        // Test-render a small buffer to detect unsupported chip types.
        // GME opens VGM files with any chip but silently outputs zeros
        // for chips it cannot emulate (e.g. NES 2A03, arcade chips).
        {
            short testBuf[2048];
            gme_err_t testErr = gme_play(_emu, 2048, testBuf);
            if (testErr)
            {
                _lastError = std::string("GME play: ") + testErr;
                gme_delete(_emu);
                _emu = nullptr;
                return false;
            }
            int nonZero = 0;
            for (int i = 0; i < 2048; ++i)
            {
                if (testBuf[i] != 0) nonZero++;
            }
            if (nonZero == 0)
            {
                _lastError = "Unsupported VGM chip (try NSF/SPC for this system)";
                gme_delete(_emu);
                _emu = nullptr;
                return false;
            }
            // Restart track after test render
            gme_start_track(_emu, 0);
        }

        if (_currentTrack.duration > 0.0f)
        {
            gme_set_fade(_emu, static_cast<int>(_currentTrack.duration * 1000.0f));
        }

        _duration.store(_currentTrack.duration, std::memory_order_relaxed);
    }
    else
    {
        // Use miniaudio decoder for MP3/OGG/FLAC
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
    }

    _framesPlayed = 0;
    _position.store(0.0f, std::memory_order_relaxed);
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
    if (!_decoder && !_emu)
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
    // has fully returned, so after this point no thread touches
    // _decoder or _emu.
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

    if (_emu)
    {
        gme_delete(_emu);
        _emu = nullptr;
    }

    _trackEnded.store(false, std::memory_order_relaxed);
    _framesPlayed = 0;
    _position.store(0.0f, std::memory_order_relaxed);
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

    if (_currentFormat == AudioFormat::Gme)
    {
        if (_emu)
        {
            gme_seek(_emu, static_cast<int>(seconds * 1000.0f));
            _framesPlayed = static_cast<uint64_t>(seconds * SAMPLE_RATE);
            _position.store(seconds, std::memory_order_relaxed);
        }
    }
    else
    {
        if (_decoder)
        {
            ma_uint64 frame = static_cast<ma_uint64>(seconds * SAMPLE_RATE);
            ma_decoder_seek_to_pcm_frame(_decoder, frame);
            _framesPlayed = frame;
            _position.store(seconds, std::memory_order_relaxed);
        }
    }
}

void AudioEngine::setVolume(float v)
{
    _volume.store(std::clamp(v, 0.0f, 1.0f), std::memory_order_relaxed);
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
    unsigned int totalSamples = frameCount * CHANNELS;

    std::lock_guard<std::mutex> lock(_audioMutex);

    if (_currentFormat == AudioFormat::Gme)
    {
        if (!_emu)
        {
            std::memset(output, 0, totalSamples * sizeof(float));
            _trackEnded.store(true, std::memory_order_release);
            return;
        }

        // GME outputs 16-bit stereo samples — use stack buffer for small frames,
        // heap for large ones
        short stackBuf[4096];
        short * buf16 = stackBuf;
        bool heapAlloc = false;
        if (totalSamples > 4096)
        {
            buf16 = new short[totalSamples];
            heapAlloc = true;
        }

        gme_err_t err = gme_play(_emu, static_cast<int>(totalSamples), buf16);

        if (err || gme_track_ended(_emu))
        {
            std::memset(output, 0, totalSamples * sizeof(float));
            if (heapAlloc) delete[] buf16;
            _trackEnded.store(true, std::memory_order_release);
            return;
        }

        // Convert int16 -> float and apply volume
        for (unsigned int i = 0; i < totalSamples; ++i)
        {
            output[i] = (static_cast<float>(buf16[i]) / 32768.0f) * vol;
        }
        if (heapAlloc) delete[] buf16;
    }
    else if (_decoder)
    {
        ma_uint64 framesRead = 0;
        ma_decoder_read_pcm_frames(_decoder, output, frameCount, &framesRead);

        if (framesRead < frameCount)
        {
            // End of file — zero remaining
            std::memset(output + framesRead * CHANNELS, 0,
                        (frameCount - framesRead) * CHANNELS * sizeof(float));
            _trackEnded.store(true, std::memory_order_release);
        }

        // Apply volume
        unsigned int samplesRead = static_cast<unsigned int>(framesRead) * CHANNELS;
        for (unsigned int i = 0; i < samplesRead; ++i)
        {
            output[i] *= vol;
        }
    }
    else
    {
        std::memset(output, 0, totalSamples * sizeof(float));
        return;
    }

    // Feed visualization buffer (mono mixdown)
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

} // namespace vtamp
