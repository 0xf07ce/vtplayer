// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <cmath>
#include <complex>

namespace vtplayer
{

/// Simple in-place radix-2 FFT (decimation in time)
template <int N>
class Fft
{
public:
    static_assert((N & (N - 1)) == 0, "N must be power of 2");

    using Complex = std::complex<float>;

    void forward(float const * input, Complex * output) const
    {
        // Copy with bit-reversal permutation
        for (int i = 0; i < N; ++i)
        {
            output[bitReverse(i)] = Complex(input[i], 0.0f);
        }

        // Butterfly stages
        for (int size = 2; size <= N; size *= 2)
        {
            int half = size / 2;
            float angle = -2.0f * PI / static_cast<float>(size);
            Complex wn(std::cos(angle), std::sin(angle));

            for (int i = 0; i < N; i += size)
            {
                Complex w(1.0f, 0.0f);
                for (int j = 0; j < half; ++j)
                {
                    Complex t = w * output[i + j + half];
                    Complex u = output[i + j];
                    output[i + j] = u + t;
                    output[i + j + half] = u - t;
                    w *= wn;
                }
            }
        }
    }

    /// Compute magnitude spectrum (first N/2 bins)
    void magnitude(Complex const * fftData, float * mag) const
    {
        for (int i = 0; i < N / 2; ++i)
        {
            mag[i] = std::abs(fftData[i]);
        }
    }

private:
    static constexpr float PI = 3.14159265358979323846f;

    static constexpr int bitReverse(int x)
    {
        int result = 0;
        int bits = LOG2N;
        for (int i = 0; i < bits; ++i)
        {
            result = (result << 1) | (x & 1);
            x >>= 1;
        }
        return result;
    }

    static constexpr int log2(int n)
    {
        int r = 0;
        while (n > 1) { n >>= 1; ++r; }
        return r;
    }

    static constexpr int LOG2N = log2(N);
};

} // namespace vtplayer
