// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// OrbitCapture DSP — Farina deconvolution.
//
// COMPAT SHIM (v0.7.0): delegates to felitronics-core (`felitronics::measurement`) —
// core owns the deconvolution + onset search; this keeps the `oc::` spelling + field names
// so Main.cpp and the dsp tests compile unchanged. See felitronics/measurement/Deconvolve.h.
#pragma once
#include "fft.hpp"
#include "sweep.hpp"
#include "post.hpp"
#include <felitronics/measurement/Deconvolve.h>
#include <vector>
#include <cstddef>

namespace oc {

struct DeconvResult {
    std::vector<double> full;    // recording ⊛ inverse
    std::size_t linear_lag = 0;  // index of the linear IR's onset (latency-corrected)
    std::size_t latency    = 0;  // measured round-trip τ, samples
    double sr = 0.0;
};

inline DeconvResult deconvolve(const std::vector<double>& recording, const Sweep& sw) {
    // Rebuild the minimal core Sweep the deconvolver reads (inverse + sweepLen + sample rate).
    felitronics::measurement::Sweep ms;
    ms.inverse = sw.inv; ms.sweepLen = sw.sweep_len; ms.spec.sampleRate = sw.spec.sr;
    const felitronics::measurement::DeconvResult m = felitronics::measurement::deconvolve(recording, ms);

    DeconvResult r;
    r.full = m.full; r.linear_lag = m.linearLag; r.latency = m.latencySamples; r.sr = m.sampleRate;
    return r;
}

// Copy the linear IR window [linear_lag, linear_lag+len) out of the deconvolution.
inline std::vector<double> extract_ir(const DeconvResult& d, std::size_t len) {
    felitronics::measurement::DeconvResult m;
    m.full = d.full; m.linearLag = d.linear_lag;
    return felitronics::measurement::extractIr(m, len);
}

} // namespace oc
