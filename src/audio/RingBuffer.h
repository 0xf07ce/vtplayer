// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <atomic>
#include <array>
#include <cstring>

namespace vtamp
{

/// SPSC lock-free ring buffer for passing audio samples from audio thread to UI thread
template <typename T, int N>
class RingBuffer
{
public:
    void write(T const * data, int count)
    {
        auto w = _writePos.load(std::memory_order_relaxed);
        for (int i = 0; i < count; ++i)
        {
            _buffer[w & MASK] = data[i];
            w++;
        }
        _writePos.store(w, std::memory_order_release);
    }

    /// Read latest `count` samples (most recent data)
    int readLatest(T * out, int count) const
    {
        auto w = _writePos.load(std::memory_order_acquire);
        if (count > N)
        {
            count = N;
        }
        auto start = w - count;
        for (int i = 0; i < count; ++i)
        {
            out[i] = _buffer[(start + i) & MASK];
        }
        return count;
    }

private:
    static constexpr int MASK = N - 1;
    static_assert((N & (N - 1)) == 0, "N must be power of 2");

    std::array<T, N> _buffer{};
    std::atomic<uint64_t> _writePos{0};
};

} // namespace vtamp
