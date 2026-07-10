// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
// OrbitCapture — take.json ↔ MixModel bridge (juce_core only). Keeps the JSON schema + the THREE legacy
// migrations out of the pure model + engine: invert→phase_deg, fx_on/filter_on→hpf_on/lpf_on, and the
// v1 global blend_*→Master object. De-monolith step 3. (Ported verbatim from readStrip/writeStrip +
// loadTake's master migration so old take.json files keep loading identically.)
#include "model/MixModel.h"
#include <juce_core/juce_core.h>

namespace ocap {

inline StripParams stripFromVar (const juce::var& mv) {
    StripParams p;
    p.gainDb   = (double) mv.getProperty ("gain_db", 0.0);
    p.phaseDeg = (double) mv.getProperty ("phase_deg", (bool) mv.getProperty ("invert", false) ? 180.0 : 0.0);
    p.shiftMs  = (double) mv.getProperty ("shift_ms", 0.0);
    p.solo     = (bool)   mv.getProperty ("solo", false);
    p.mute     = (bool)   mv.getProperty ("mute", false);
    const bool fxLegacy     = (bool) mv.getProperty ("fx_on", false);          // v0 per-mic FX gate
    const bool filterLegacy = (bool) mv.getProperty ("filter_on", fxLegacy);   // v2 single Filter toggle
    double hz = (double) mv.getProperty ("hpf_hz", 80.0);
    if (hz <= kHpfLo + 0.5) hz = 80.0;                                          // old off-sentinel → default
    p.hpf = { (bool) mv.getProperty ("hpf_on", filterLegacy), juce::jlimit (kHpfLo, kHpfHi, hz), (int) mv.getProperty ("hpf_slope", 24) };
    double lz = (double) mv.getProperty ("lpf_hz", 8000.0);
    if (lz >= kLpfHi - 0.5) lz = 8000.0;
    p.lpf = { (bool) mv.getProperty ("lpf_on", filterLegacy), juce::jlimit (kLpfLo, kLpfHi, lz), (int) mv.getProperty ("lpf_slope", 12) };
    return p;
}

inline void stripToVar (juce::DynamicObject* mo, const StripParams& p) {
    mo->setProperty ("gain_db", p.gainDb);
    mo->setProperty ("hpf_on", p.hpf.on); mo->setProperty ("lpf_on", p.lpf.on);
    mo->setProperty ("hpf_hz", p.hpf.hz); mo->setProperty ("hpf_slope", p.hpf.slopeDb);
    mo->setProperty ("lpf_hz", p.lpf.hz); mo->setProperty ("lpf_slope", p.lpf.slopeDb);
    mo->setProperty ("phase_deg", p.phaseDeg);
    mo->setProperty ("shift_ms", p.shiftMs);
    mo->setProperty ("solo", p.solo);
    mo->setProperty ("mute", p.mute);
}

// The Master bus: the new "master" object if present, else migrate the v1 global blend_* keys.
inline MasterParams masterFromVar (const juce::var& takeRoot) {
    const auto master = takeRoot.getProperty ("master", juce::var());
    if (master.isObject()) {
        const StripParams s = stripFromVar (master);      // master reuses the filter/gain parse (no phase/shift/solo/mute)
        return { s.gainDb, s.hpf, s.lpf };
    }
    MasterParams p;                                       // migrate v1 global blend_* → Master
    const int hs = (int) takeRoot.getProperty ("blend_hpf_slope", 0);
    p.hpf = { (bool) takeRoot.getProperty ("blend_hpf_on", hs > 0),
              juce::jlimit (kHpfLo, kHpfHi, (double) takeRoot.getProperty ("blend_hpf_hz", 80.0)), hs > 0 ? hs : 24 };
    const int ls = (int) takeRoot.getProperty ("blend_lpf_slope", 0);
    p.lpf = { (bool) takeRoot.getProperty ("blend_lpf_on", ls > 0),
              juce::jlimit (kLpfLo, kLpfHi, (double) takeRoot.getProperty ("blend_lpf_hz", 8000.0)), ls > 0 ? ls : 12 };
    return p;
}

inline void masterToVar (juce::DynamicObject* mo, const MasterParams& p) {
    mo->setProperty ("gain_db", p.gainDb);
    mo->setProperty ("hpf_on", p.hpf.on); mo->setProperty ("lpf_on", p.lpf.on);
    mo->setProperty ("hpf_hz", p.hpf.hz); mo->setProperty ("hpf_slope", p.hpf.slopeDb);
    mo->setProperty ("lpf_hz", p.lpf.hz); mo->setProperty ("lpf_slope", p.lpf.slopeDb);
}

} // namespace ocap
