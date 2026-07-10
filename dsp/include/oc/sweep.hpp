// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// OrbitCapture DSP — exponential sine sweep (ESS / Farina) + matched inverse filter.
//
// COMPAT SHIM (v0.7.0): delegates to felitronics-core (`felitronics::measurement`) —
// core owns the sweep/inverse math (hardened: every param clamped/healed). This keeps the
// `oc::` spelling + field names so Main.cpp and the dsp tests compile unchanged. For the
// nominal capture spec (f1=20, f2=20k, dur=5, amp=0.5, …) the output is identical to the
// pre-shim code. See felitronics/measurement/Sweep.h.
#pragma once
#include "fft.hpp"
#include <felitronics/measurement/Sweep.h>
#include <vector>
#include <cstddef>

namespace oc {

struct SweepSpec {
    double f1   = 20.0;
    double f2   = 20000.0;
    double dur  = 5.0;
    double sr   = 48000.0;
    double amp  = 0.5;
    double fade = 0.05;
    double tail = 1.0;
};

struct Sweep {
    std::vector<double> x;         // played sweep (sweep proper + tail silence)
    std::vector<double> inv;       // Farina inverse filter (length = sweep proper)
    SweepSpec spec;                // AS-SANITIZED
    std::size_t sweep_len = 0;
    double L = 0.0;                // T / R — harmonic-advance time constant
};

inline Sweep make_sweep(SweepSpec s) {
    felitronics::measurement::SweepSpec ms;
    ms.f1 = s.f1; ms.f2 = s.f2; ms.durationSeconds = s.dur; ms.sampleRate = s.sr;
    ms.amplitude = s.amp; ms.fadeSeconds = s.fade; ms.tailSeconds = s.tail;
    const felitronics::measurement::Sweep m = felitronics::measurement::makeSweep(ms);

    Sweep out;
    out.x = m.signal;
    out.inv = m.inverse;
    out.sweep_len = m.sweepLen;
    out.L = m.harmonicL;
    out.spec.f1 = m.spec.f1; out.spec.f2 = m.spec.f2; out.spec.dur = m.spec.durationSeconds;
    out.spec.sr = m.spec.sampleRate; out.spec.amp = m.spec.amplitude;
    out.spec.fade = m.spec.fadeSeconds; out.spec.tail = m.spec.tailSeconds;
    return out;
}

// Time-advance (in samples) of the k-th harmonic image relative to the linear IR.
inline double harmonic_advance_samples(const Sweep& sw, int k) {
    return (k > 1) ? sw.L * std::log((double)k) * sw.spec.sr : 0.0;
}

} // namespace oc
