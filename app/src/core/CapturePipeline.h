// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
// OrbitCapture — the post-capture pipeline (JUCE-free, headless-testable). De-monolith step 5.
//
// gate-all → whole-set REJECT (a coherent set is the unit) → deconv-all → ONE common time reference
// (the earliest mic's onset, so inter-mic delays — the sound of a mic set — survive) → 500 ms extract →
// ONE shared 0.98 normalize (set balance preserved). The math NEVER reads mic metadata; names / colours /
// take.json all stay in CaptureComponent. Ported verbatim from handleAsyncUpdate so it's byte-identical.
#include "oc/sweep.hpp"
#include "oc/gate.hpp"
#include <felitronics/measurement/Deconvolve.h>
#include <felitronics/measurement/MicSetAlign.h>   // alignToCommonOnset + peakGain (the app used to hand-roll these)

#include <vector>
#include <cstddef>
#include <limits>
#include <cmath>
#include <algorithm>

namespace ocap {

struct CaptureResult {
    bool                             ok = false;
    std::vector<oc::GateReport>      gates;         // always N — reported even on reject
    std::vector<std::vector<double>> irs;           // empty unless ok; carry the ONE shared 0.98 gain
    std::vector<std::size_t>         delaySamples;  // linearLag[m] - refLag (the inter-mic delays)
    std::vector<std::size_t>         latency;       // drs[m].latency (round-trip τ, for take.json)
    std::size_t                      refLag = 0;
    double                           normGain = 1.0;
};

// Run the whole set through the pipeline. `recs` = the copied recordings (never the live RT buffers).
inline CaptureResult runCapturePipeline (const std::vector<std::vector<double>>& recs,
                                         const oc::Sweep& sw, double sr, double irSeconds = 0.5) {
    const std::size_t N = recs.size();
    CaptureResult r; r.gates.resize (N);
    bool anyFail = (N == 0);
    for (std::size_t m = 0; m < N; ++m) {
        r.gates[m] = oc::gate_recording (recs[m], sw);
        if (! r.gates[m].ok) anyFail = true;
    }
    if (anyFail) return r;                                          // ok stays false; gates populated for the message

    // Deconv every mic, then align to ONE common earliest onset + ONE shared 0.98 gain — all core::
    // measurement (alignToCommonOnset cuts every mic at the same absolute index; peakGain balances once).
    namespace mm = felitronics::measurement;
    mm::Sweep ms; ms.inverse = sw.inv; ms.sweepLen = sw.sweep_len; ms.spec.sampleRate = sw.spec.sr;
    std::vector<mm::DeconvResult> drs (N);
    for (std::size_t m = 0; m < N; ++m) drs[m] = mm::deconvolve (recs[m], ms);

    const std::size_t irLen = (std::size_t) std::lround (sr * irSeconds);
    const mm::AlignedIrSet aligned = mm::alignToCommonOnset (drs, irLen);
    r.refLag       = aligned.refLag;
    r.irs          = aligned.irs;
    r.delaySamples = aligned.delaySamples;
    r.latency.resize (N);
    for (std::size_t m = 0; m < N; ++m) r.latency[m] = drs[m].latencySamples;
    r.normGain = mm::peakGain (r.irs, 0.98);                        // ONE gain for the set (balance preserved)
    for (auto& o : r.irs) mm::applyGain (o, r.normGain);
    r.ok = true;
    return r;
}

} // namespace ocap
