// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// OrbitCapture DSP — input-validation gate BEFORE deconvolution.
//
// COMPAT SHIM (v0.7.0): delegates to felitronics-core (`felitronics::measurement`) —
// core owns the gate logic (hardened: non-finite pre-scan, empty-inverse reject, band
// clamp). This keeps the `oc::` spelling + field names so Main.cpp and the dsp tests
// compile unchanged. Core returns a GateReject ENUM (user strings are app-side); this shim
// maps it back to the human `reason` string Main.cpp expects. See measurement/CaptureGate.h.
#pragma once
#include "fft.hpp"
#include "sweep.hpp"
#include <felitronics/measurement/CaptureGate.h>
#include <vector>
#include <string>
#include <cstddef>

namespace oc {

struct GateReport {
    bool        ok            = true;
    bool        clipped       = false;
    double      peak_dbfs     = -120.0;
    int         clip_run      = 0;
    bool        sweep_present = false;
    double      snr           = 0.0;  // deconv peak / early floor (presence ratio, NOT a real SNR)
    double      snr_db        = 0.0;  // honest band-limited measurement SNR
    std::size_t sweep_lag     = 0;
    std::string reason;
};

inline std::string gate_reason_string(felitronics::measurement::GateReject r) {
    using felitronics::measurement::GateReject;
    switch (r) {
        case GateReject::None:            return "";
        case GateReject::EmptyRecording:  return "empty recording";
        case GateReject::NonFinite:       return "non-finite samples in recording";
        case GateReject::Clipped:         return "clipped (nonlinearity would bleed into IR)";
        case GateReject::SweepNotDetected:return "sweep not detected";
    }
    return "";
}

// Band-limited (100 Hz–6 kHz) energy of a window — the guitar-cab working band.
inline double oc_band_energy(const std::vector<double>& x, double sr) {
    return felitronics::measurement::bandEnergy(x, sr, 100.0, 6000.0);
}

inline GateReport gate_recording(const std::vector<double>& rec, const Sweep& sw,
                                 double clip_level = 0.999, int clip_run = 3,
                                 double min_snr = 1000.0) {
    felitronics::measurement::Sweep ms;
    ms.inverse = sw.inv; ms.spec.sampleRate = sw.spec.sr;
    felitronics::measurement::GateConfig cfg;
    cfg.clipLevel = clip_level; cfg.clipRunSamples = clip_run; cfg.minSweepConfidence = min_snr;
    const felitronics::measurement::GateReport m = felitronics::measurement::gateRecording(rec, ms, cfg);

    GateReport g;
    g.ok = m.ok; g.clipped = m.clipped; g.peak_dbfs = m.peakDbfs; g.clip_run = m.clipRun;
    g.sweep_present = m.sweepPresent; g.snr = m.sweepConfidence; g.snr_db = m.snrDb;
    g.sweep_lag = m.sweepLag; g.reason = gate_reason_string(m.reason);
    return g;
}

} // namespace oc
