// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
// OrbitCapture — IR deliverable math (JUCE-free, headless-testable).
//
// Sample-rate conversion comes from core's convolution::IrResampler (Kaiser windowed-sinc) — the
// family's ONE resampler fingerprint (reuse audit W4). The swap off the local Blackman-sinc was
// gated by a measured A/B (same 128-tap radius): passband identical to the milli-dB (both -3.010
// dB tone RMS across 1k..19k on 48->44.1), DC exact for both, alias rejection of a 23 kHz tone
// -115.5 dB (Kaiser beta=10) vs -95.8 dB (old Blackman) — strictly better, everything else equal.
// NB deliberate: core returns float (24-bit mantissa, the same depth as the 24-bit PCM
// deliverable); the old path stayed double end-to-end, so low-order deliverable bits may differ.
#include <felitronics/convolution/IrResampler.h>

#include <vector>
#include <cmath>
#include <algorithm>
#include <cstddef>

namespace ocap {

inline std::vector<double> resampleIR(const std::vector<float>& x, double srIn, double srOut) {
    if (std::abs(srIn - srOut) < 1.0) return { x.begin(), x.end() };
    felitronics::convolution::IrResampleConfig cfg;
    cfg.halfTaps = 64;            // 128-tap radius — matches the old kernel's sharpness
    cfg.beta = 10.0;              // ~100 dB-class stopband (measured -115 dB on the 23 kHz probe)
    cfg.cutoffScale = 0.95;       // 0.475 x the lower Nyquist — the same passband edge as before
    const auto y = felitronics::convolution::resampleIr(x, srIn, srOut, cfg);
    return { y.begin(), y.end() };
}

} // namespace ocap
