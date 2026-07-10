// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
// OrbitCapture — auto time/polarity alignment of mixer channels (JUCE-free, std only).
//
// The "Auto" button's maths: for each channel, the normalized cross-correlation against the
// reference channel over a window straddling the onsets picks the best sub-knob-range lag
// (|shift| <= the strip knob's ±2 ms) and the polarity (a negative correlation peak = the mic
// is flipped). The result maps straight onto strip params: shiftMs (positive = delay, matching
// felitronics::blend::shiftFrac) and a 180° phase (= polarity) — the phase knob stays free for
// manual seasoning. This undoes comb filtering from residual mic-to-mic offsets; gross offsets
// are already handled by onset alignment at import/capture time.
#include "core/IrImport.h"   // onsetIndex

#include <cmath>
#include <cstddef>
#include <vector>

namespace ocap::autoalign {

struct Alignment {
    double shiftMs = 0.0;      // apply to the strip's shift knob (positive = delay this channel)
    bool   invert = false;     // apply as phase ±180° (polarity flip)
    double corr = 0.0;         // |peak| of the normalized cross-correlation (0..1, confidence)
};

// Align `ch` against `ref`: the best lag within ±maxShiftMs by cross-correlation over an
// 8k-sample window from the earlier onset. corr stays 0 when there's nothing to correlate.
inline Alignment alignOne(const std::vector<float>& ref, const std::vector<float>& ch,
                          double sr, double maxShiftMs = 2.0) {
    Alignment out;
    if (ref.empty() || ch.empty() || sr <= 0) return out;
    const int maxLag = (int)std::lround(maxShiftMs * sr / 1000.0);
    const int start = (int)std::max<std::ptrdiff_t>(
        0, std::min(irimport::onsetIndex(ref), irimport::onsetIndex(ch)) - maxLag);
    const int win = std::min<int>(8192, (int)std::min(ref.size(), ch.size()) - start - maxLag - 1);
    if (win <= 16 || maxLag <= 0) return out;
    double eR = 0, eC = 0;
    for (int i = 0; i < win; ++i) {
        eR += (double)ref[(size_t)(start + i)] * ref[(size_t)(start + i)];
        eC += (double)ch[(size_t)(start + i)]  * ch[(size_t)(start + i)];
    }
    if (eR <= 0 || eC <= 0) return out;
    double best = 0; int bestLag = 0;
    for (int lag = -maxLag; lag <= maxLag; ++lag) {
        double s = 0;
        for (int i = 0; i < win; ++i) {
            const int j = start + i + lag;
            if (j >= 0 && j < (int)ch.size()) s += (double)ref[(size_t)(start + i)] * ch[(size_t)j];
        }
        if (std::abs(s) > std::abs(best)) { best = s; bestLag = lag; }
    }
    out.corr = std::abs(best) / std::sqrt(eR * eC);
    out.invert = best < 0;                             // the peak is anti-phase → flip polarity
    // ch correlates best when read `bestLag` LATER than ref → advance it by bestLag samples.
    out.shiftMs = -(double)bestLag * 1000.0 / sr;
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
