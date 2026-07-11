// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
// OrbitCapture — auto time/polarity alignment of mixer channels.
//
// ADAPTER over felitronics::measurement::xcorrAlign (the maths was PROMOTED to core — reuse audit
// N1 — and hardened there behind an adversarial review: per-lag normalization with a Cauchy-Schwarz
// corr ≤ 1 proof, zero-confidence refusal when the onsets sit further apart than the search range,
// subnormal-safe denominators, brute-force-oracle NULL tests). This shim only converts the app's
// units: float buffers → double spans, ±ms → ±samples, shiftSamples → the strip knob's shiftMs.
//
// corr == 0 means "no confident suggestion" — the caller must leave that channel UNTOUCHED
// (an auto-align that guesses under uncertainty creates the combing it exists to remove).
#include <felitronics/measurement/XcorrAlign.h>

#include <cmath>
#include <cstddef>
#include <vector>

namespace ocap::autoalign {

struct Alignment {
    double shiftMs = 0.0;      // apply to the strip's shift knob (positive = delay this channel)
    bool   invert = false;     // apply as phase ±180° (polarity flip)
    double corr = 0.0;         // confidence 0..1; 0 = leave the channel alone
};

inline Alignment alignOne(const std::vector<float>& ref, const std::vector<float>& ch,
                          double sr, double maxShiftMs = 2.0) {
    Alignment out;
    if (sr <= 0.0 || ref.empty() || ch.empty()) return out;
    const int maxLag = (int)std::lround(maxShiftMs * sr / 1000.0);
    const std::vector<double> r(ref.begin(), ref.end()), c(ch.begin(), ch.end());
    const auto a = felitronics::measurement::xcorrAlign(r, c, maxLag);
    out.shiftMs = a.shiftSamples * 1000.0 / sr;
    out.invert  = a.invert;
    out.corr    = a.corr;
    return out;
}

// Whole console vs irs[ref]; entry [ref] stays identity (it IS the time reference).
inline std::vector<Alignment> align(const std::vector<std::vector<float>>& irs, double sr,
                                    std::size_t ref, double maxShiftMs = 2.0) {
    std::vector<Alignment> out(irs.size());
    if (ref >= irs.size()) return out;
    for (std::size_t m = 0; m < irs.size(); ++m)
        if (m != ref) out[m] = alignOne(irs[ref], irs[m], sr, maxShiftMs);
    return out;
}

} // namespace ocap::autoalign
