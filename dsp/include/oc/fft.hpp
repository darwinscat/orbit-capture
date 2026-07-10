// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// OrbitCapture DSP — FFT + linear convolution.
//
// COMPAT SHIM (v0.7.0): the implementation now lives in felitronics-core
// (`felitronics::core::offline`) — core owns the one radix-2 double FFT so the app
// stops carrying its own copy. This header keeps the `oc::` spelling so Main.cpp and
// the dsp tests/tools compile unchanged; the math is byte-identical (core::offline was
// extracted from this very file). See felitronics/core/OfflineFft.h.
#pragma once
#include <felitronics/core/OfflineFft.h>
#include <complex>
#include <vector>
#include <cstddef>

namespace oc {

inline constexpr double kPi = felitronics::core::kPi;
using cd = std::complex<double>;

inline std::size_t next_pow2(std::size_t n) { return felitronics::core::offline::nextPow2(n); }

// sign = -1 forward, +1 inverse (inverse NOT normalized). a.size() must be a power of two.
inline void fft_inplace(std::vector<cd>& a, int sign) { felitronics::core::offline::detail::fftInplace(a, sign); }

inline std::vector<cd> fft(std::vector<cd> a)  { fft_inplace(a, -1); return a; }
inline std::vector<cd> ifft(std::vector<cd> a) {
    fft_inplace(a, +1);
    const double inv = 1.0 / (double)a.size();
    for (auto& x : a) x *= inv;
    return a;
}

// Real linear convolution via FFT. Returns length x.size()+h.size()-1.
inline std::vector<double> convolve(const std::vector<double>& x, const std::vector<double>& h) {
    return felitronics::core::offline::convolve(x, h);
}

// Magnitude spectrum (linear) of a real signal, zero-padded to `nfft` (pow2). Length nfft/2.
inline std::vector<double> mag_spectrum(const std::vector<double>& x, std::size_t nfft) {
    return felitronics::core::offline::magSpectrum(x, nfft);
}

} // namespace oc
