// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
// OrbitCapture — audition render math (JUCE-free, headless-testable). De-monolith step 2, lifted
// verbatim from CaptureComponent. Convolve a DI clip through an IR for the wet audition preview.
#include "oc/fft.hpp"   // oc::convolve
#include <vector>

namespace ocap {

inline std::vector<float> convolveDI(const std::vector<float>& di, const std::vector<float>& ir) {
    std::vector<double> a(di.begin(), di.end()), b(ir.begin(), ir.end());
    const std::vector<double> w = oc::convolve(a, b);
    return { w.begin(), w.end() };
}

} // namespace ocap
