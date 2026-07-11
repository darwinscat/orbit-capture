// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
// OrbitCapture — session/take persistence data model (JUCE-free, std only). De-monolith step 6.
// Mirrors the take.json / session.json schema written by Main.cpp's saveTakeToSession / saveSessionJson
// (the authoritative writer, ~line 1583) — kept here so SessionStore + tests can build/inspect it without
// pulling in JUCE. The juce_core <-> these structs bridge lives in model/SessionVar.h.
#include "model/MixModel.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ocap {

// One entry of take.json's mics[] (and its v1-compat "mic" mirror, mics[0]).
struct MicMeta {
    std::string model, location, position, axis, distanceInput;
    int         distanceMm = 0, inputChannel = 0, slot = 0;
    double      gatePeakDbfs = 0.0, gateSnr = 0.0, snrDb = 0.0;
    int         latencySamples = 0;
    std::size_t delaySamples = 0;
};

// take.json's sweep{} — the sweep spec used for this capture (reproducibility).
struct SweepMeta {
    double f1 = 0.0, f2 = 0.0, dur = 0.0, tail = 0.0;
};

// take.json's measured{} — primary-mic (mics[0]) derived metrics, v1 compat.
struct MeasuredMeta {
    double      gatePeakDbfs = 0.0, gateSnr = 0.0, snrDb = 0.0;
    int         clipRun = 0, latencySamples = 0;
    std::size_t irLen = 0;
};

// One take.json — a single capture: raw + IR audio (on disk, alongside) + its mix state.
// "interface_" avoids colliding with the C++ keyword; the JSON key is still "interface".
struct TakeMeta {
    std::string app, timestamp, interface_;
    std::string name;                     // optional user-given take name (empty = synthetic label)
    double      sampleRate = 0.0;
    int         inputChannel = 0;
    // cabinet + amp + room snapshot at capture time (kept as strings, mirroring the UI combo-box text —
    // NOT re-derived/re-validated here; that stays app-side).
    std::string instrument, enclosure, cabModel, speaker, speakerSizeIn, back, ampModel, ampType, room;
    int         speakerCount = 0;
    bool        tweeter = false;
    std::vector<MicMeta>       mics;
    MeasuredMeta                measured;
    SweepMeta                   sweep;
    std::vector<StripParams>    mix;
    MasterParams                master;
    std::optional<StripParams>  monoFilter;   // single-mic HPF/LPF (only meaningful when mics.size() < 2)
};

// One session.json — a cabinet-capture session (many takes). All-string, mirroring exactly what
// saveSessionJson persists (raw combo-box text, not parsed numerics) — so unknown/blank UI states
// round-trip untouched.
struct SessionMeta {
    std::string instrument, enclosure, cabModel, speaker, speakerCount, speakerSizeIn,
                tweeter, back, amp, ampType, room, author, distUnit;
};

} // namespace ocap
