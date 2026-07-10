// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// OrbitCapture DSP — post-processing of the deconvolved IR (onset / trim / peak gain).
//
// COMPAT SHIM (v0.7.0): delegates to felitronics-core (`felitronics::measurement`) —
// core owns the algorithm; this keeps the `oc::` spelling + field names so Main.cpp and
// the dsp tests compile unchanged. See felitronics/measurement/IrPost.h.
#pragma once
#include <felitronics/measurement/IrPost.h>
#include <vector>
#include <cstddef>

namespace oc {

namespace mm = felitronics::measurement;

inline double peak_gain(const std::vector<double>& x, double target_peak = 0.98) {
    return mm::peakGain(x, target_peak);
}
inline void apply_gain(std::vector<double>& x, double g) { mm::applyGain(x, g); }

struct OnsetResult {
    std::size_t onset    = 0;
    std::size_t peak_idx = 0;
    double      peak     = 0.0;
};

inline OnsetResult detect_onset(const std::vector<double>& ir,
                                double thresh_db = -40.0,
                                std::size_t pre_roll = 8,
                                std::size_t search_start = 0) {
    const mm::OnsetResult r = mm::detectOnset(ir, thresh_db, pre_roll, search_start);
    return { r.onset, r.peakIndex, r.peak };
}

inline std::vector<double> trim_to_onset(const std::vector<double>& ir,
                                         const OnsetResult& on,
                                         std::size_t len = 0) {
    mm::OnsetResult r; r.onset = on.onset; r.peakIndex = on.peak_idx; r.peak = on.peak;
    return mm::trimToOnset(ir, r, len);
}

} // namespace oc
