// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "StreamSource.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char ** environ;

namespace vtplayer
{

namespace
{

// Samples per second of interleaved stereo float audio.
constexpr std::size_t kSamplesPerSec =
    static_cast<std::size_t>(StreamSource::kSampleRate)
    * StreamSource::kChannels;

// Locate an executable named `name` on PATH. Empty if not found.
std::string findOnPath(char const * name)
{
    char const * path = std::getenv("PATH");
    if (!path)
        return {};
    std::string const dirs = path;
    std::size_t start = 0;
    while (start <= dirs.size())
    {
        std::size_t const colon = dirs.find(':', start);
        std::string const dir =
            dirs.substr(start, colon == std::string::npos ? std::string::npos
                                                          : colon - start);
        if (!dir.empty())
        {
            std::string cand = dir + "/" + name;
            if (::access(cand.c_str(), X_OK) == 0)
                return cand;
        }
        if (colon == std::string::npos)
            break;
        start = colon + 1;
    }
    return {};
}

} // namespace

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

    std::string const ffmpeg = findOnPath("ffmpeg");
    if (ffmpeg.empty())
    {
        _error = "ffmpeg not found on PATH (install it: brew install ffmpeg)";
        return false;
    }

    int fds[2];
    if (::pipe(fds) != 0)
    {
        _error = "pipe() failed";
        return false;
    }

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    // Child stdout → pipe write end; close the read end in the child.
    posix_spawn_file_actions_adddup2(&fa, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&fa, fds[0]);
    posix_spawn_file_actions_addclose(&fa, fds[1]);
    // Normal mode: silence ffmpeg's stderr so transient HTTP/reconnect
    // diagnostics ("Error reading HTTP response: End of file") never bleed
    // onto the TUI. Debug mode leaves stderr inherited (prints to terminal).
    if (!_debug)
        posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null",
                                         O_WRONLY, 0);

    std::string sr = std::to_string(kSampleRate);
    std::string ac = std::to_string(kChannels);
    // -nostdin: never read our stdin. -reconnect*: survive transient network
    // drops. -vn: audio only. Output raw f32le so no decoder is needed here.
    char const * argv[] = {
        "ffmpeg",      "-nostdin",   "-hide_banner", "-loglevel", "error",
        "-reconnect",  "1",          "-reconnect_streamed", "1",
        "-reconnect_delay_max", "5", "-i",           url.c_str(),
        "-vn",         "-f",         "f32le",        "-ar", sr.c_str(),
        "-ac",         ac.c_str(),   "-",            nullptr};

    pid_t pid = -1;
    int const rc = posix_spawn(&pid, ffmpeg.c_str(), &fa, nullptr,
                               const_cast<char * const *>(argv), environ);
    posix_spawn_file_actions_destroy(&fa);
    ::close(fds[1]); // parent only reads

    if (rc != 0)
    {
        ::close(fds[0]);
        _error = "failed to spawn ffmpeg";
        return false;
    }

    _pid    = pid;
    _readFd = fds[0];
    _reader = std::thread(&StreamSource::readerLoop, this);
    return true;
}

void StreamSource::stop()
{
    _stopFlag.store(true);
    _cv.notify_all(); // wake a reader parked on backpressure

    if (_pid > 0)
    {
        ::kill(_pid, SIGKILL);
    }
    if (_readFd >= 0)
    {
        ::close(_readFd); // unblocks a reader thread parked in read()
        _readFd = -1;
    }
    if (_reader.joinable())
        _reader.join();
    if (_pid > 0)
    {
        int status = 0;
        ::waitpid(_pid, &status, 0);
        _pid = -1;
    }

    _buffering.store(true);
    std::lock_guard<std::mutex> lk(_mtx);
    _ring.clear();
    _head = _size = 0;
}

void StreamSource::readerLoop()
{
    std::vector<float> chunk(4096);
    while (!_stopFlag.load())
    {
        ssize_t const n = ::read(_readFd, chunk.data(),
                                 chunk.size() * sizeof(float));
        if (n <= 0)
        {
            // 0 = ffmpeg closed stdout (EOF); <0 = error / fd closed.
            _eofFlag.store(true);
            return;
        }
        std::size_t const got = static_cast<std::size_t>(n) / sizeof(float);

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
                // The stalled pipe read propagates flow control to ffmpeg.
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
