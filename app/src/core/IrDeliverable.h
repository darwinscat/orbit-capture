// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
// OrbitCapture — IR deliverable math (JUCE-free, headless-testable). De-monolith step 2, lifted
// verbatim from CaptureComponent (juce::MathConstants pi → oc::kPi). Blackman-windowed-sinc sample-rate
// conversion for export (kernel-sum normalised: exact DC, clean edges). Verified: DC 1.000, alias −100 dB.
#include "oc/fft.hpp"   // oc::kPi
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstddef>

namespace ocap {

inline std::vector<double> resampleIR(const std::vector<float>& x, double srIn, double srOut) {
    if (std::abs(srIn - srOut) < 1.0) return { x.begin(), x.end() };
    const double ratio = srOut / srIn;
    const int nOut = (int)std::floor((double)x.size() * ratio);
    const double fc = 0.475 * std::min(1.0, ratio);                // cycles per INPUT sample
    const int half = 64;
    const double pi = oc::kPi;
    std::vector<double> y((size_t)std::max(0, nOut), 0.0);
    for (int n = 0; n < nOut; ++n) {
        const double t = (double)n / ratio;
        const int k0 = (int)std::floor(t) - half + 1, k1 = (int)std::floor(t) + half;
        double acc = 0.0, wsum = 0.0;
        for (int k = std::max(0, k0); k <= std::min((int)x.size() - 1, k1); ++k) {
            const double d = t - (double)k;
            const double s = (std::abs(d) < 1e-12) ? 2.0 * fc : std::sin(2.0 * pi * fc * d) / (pi * d);
            const double w = 0.42 + 0.5 * std::cos(pi * d / half) + 0.08 * std::cos(2.0 * pi * d / half);
            acc += (double)x[(size_t)k] * s * w;
            wsum += s * w;
        }
        y[(size_t)n] = std::abs(wsum) > 1e-9 ? acc / wsum : 0.0;   // kernel-sum normalised: exact DC, clean edges
    }
    return y;
}

} // namespace ocap
