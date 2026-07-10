// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
// OrbitCapture — take.json / session.json <-> SessionModel bridge (juce_core only). De-monolith step 6.
// Ported verbatim from Main.cpp's saveTakeToSession / loadTake / saveSessionJson / loadSessionJson so
// existing session folders on disk keep loading identically. Delegates the per-strip mix/master parse to
// MixVar.h (stripFromVar/masterFromVar/stripToVar/masterToVar) — same three legacy migrations apply
// (invert->phase_deg, fx_on/filter_on->hpf_on/lpf_on, v1 global blend_*->Master).
//
// Fixes ONE latent bug found while extracting: Main.cpp's loadTake parses only the `mics` array and
// leaves N=0 (silently dropping the mic) for pre-`mics` takes that only ever wrote the v1 `mic` mirror.
// takeFromVar falls back to `[mic]` when `mics` is absent/empty.
#include "model/SessionModel.h"
#include "model/MixVar.h"

#include <juce_core/juce_core.h>

namespace ocap {

inline MicMeta micFromVar (const juce::var& mv) {
    MicMeta m;
    m.model          = mv.getProperty ("model", "").toString().toStdString();
    m.location       = mv.getProperty ("location", "").toString().toStdString();
    m.position       = mv.getProperty ("position", "").toString().toStdString();
    m.axis           = mv.getProperty ("axis", "").toString().toStdString();
    m.distanceInput  = mv.getProperty ("distance_input", "").toString().toStdString();
    m.distanceMm     = (int) mv.getProperty ("distance_mm", 0);
    m.inputChannel   = (int) mv.getProperty ("input_channel", 0);
    m.slot           = (int) mv.getProperty ("slot", 0);
    m.gatePeakDbfs   = (double) mv.getProperty ("gate_peak_dbfs", 0.0);
    m.gateSnr        = (double) mv.getProperty ("gate_snr", 0.0);
    m.snrDb          = (double) mv.getProperty ("snr_db", 0.0);
    m.latencySamples = (int) mv.getProperty ("latency_samples", 0);
    m.delaySamples   = (std::size_t) (int) mv.getProperty ("delay_samples", 0);
    return m;
}

inline juce::var micToVar (const MicMeta& m) {
    auto* o = new juce::DynamicObject();
    o->setProperty ("model", juce::String (m.model));
    o->setProperty ("location", juce::String (m.location));
    o->setProperty ("position", juce::String (m.position));
    o->setProperty ("axis", juce::String (m.axis));
    o->setProperty ("distance_mm", m.distanceMm);
    o->setProperty ("distance_input", juce::String (m.distanceInput));
    o->setProperty ("input_channel", m.inputChannel);
    o->setProperty ("gate_peak_dbfs", m.gatePeakDbfs);
    o->setProperty ("gate_snr", m.gateSnr);
    o->setProperty ("snr_db", m.snrDb);
    o->setProperty ("latency_samples", m.latencySamples);
    o->setProperty ("delay_samples", (int) m.delaySamples);
    o->setProperty ("slot", m.slot);
    return juce::var (o);
}

inline SweepMeta sweepFromVar (const juce::var& v) {
    SweepMeta s;
    s.f1   = (double) v.getProperty ("f1", 0.0);
    s.f2   = (double) v.getProperty ("f2", 0.0);
    s.dur  = (double) v.getProperty ("dur", 0.0);
    s.tail = (double) v.getProperty ("tail", 0.0);
    return s;
}

inline juce::var sweepToVar (const SweepMeta& s) {
    auto* o = new juce::DynamicObject();
    o->setProperty ("f1", s.f1); o->setProperty ("f2", s.f2);
    o->setProperty ("dur", s.dur); o->setProperty ("tail", s.tail);
    return juce::var (o);
}

inline MeasuredMeta measuredFromVar (const juce::var& v) {
    MeasuredMeta m;
    m.gatePeakDbfs   = (double) v.getProperty ("gate_peak_dbfs", 0.0);
    m.gateSnr        = (double) v.getProperty ("gate_snr", 0.0);
    m.snrDb          = (double) v.getProperty ("snr_db", 0.0);
    m.clipRun        = (int) v.getProperty ("clip_run", 0);
    m.latencySamples = (int) v.getProperty ("latency_samples", 0);
    m.irLen          = (std::size_t) (int) v.getProperty ("ir_len", 0);
    return m;
}

inline juce::var measuredToVar (const MeasuredMeta& m) {
    auto* o = new juce::DynamicObject();
    o->setProperty ("gate_peak_dbfs", m.gatePeakDbfs);
    o->setProperty ("gate_snr", m.gateSnr);
    o->setProperty ("snr_db", m.snrDb);
    o->setProperty ("clip_run", m.clipRun);
    o->setProperty ("latency_samples", m.latencySamples);
    o->setProperty ("ir_len", (int) m.irLen);
    return juce::var (o);
}

// Parse a take.json var into a TakeMeta. See the file banner for the mics[]-fallback bug fix.
inline TakeMeta takeFromVar (const juce::var& v) {
    TakeMeta t;
    t.app        = v.getProperty ("app", "").toString().toStdString();
    t.name       = v.getProperty ("name", "").toString().toStdString();
    t.timestamp  = v.getProperty ("timestamp", "").toString().toStdString();
    t.interface_ = v.getProperty ("interface", "").toString().toStdString();
    t.sampleRate = (double) v.getProperty ("sample_rate", 0.0);
    t.inputChannel = (int) v.getProperty ("input_channel", 0);

    const auto cab = v.getProperty ("cabinet", juce::var());
    t.instrument    = cab.getProperty ("instrument", "").toString().toStdString();
    t.enclosure     = cab.getProperty ("enclosure", "").toString().toStdString();
    t.cabModel      = cab.getProperty ("model", "").toString().toStdString();
    t.speaker       = cab.getProperty ("speaker", "").toString().toStdString();
    t.speakerCount  = (int) cab.getProperty ("speaker_count", 0);
    t.speakerSizeIn = cab.getProperty ("speaker_size_in", "").toString().toStdString();
    t.tweeter       = (bool) cab.getProperty ("tweeter", false);
    t.back          = cab.getProperty ("back", "").toString().toStdString();

    const auto ampV = v.getProperty ("amp", juce::var());
    t.ampModel = ampV.getProperty ("model", "").toString().toStdString();
    t.ampType  = ampV.getProperty ("type", "").toString().toStdString();

    t.room = v.getProperty ("room", "").toString().toStdString();

    if (auto* arr = v.getProperty ("mics", juce::var()).getArray())
        for (const auto& mv : *arr) t.mics.push_back (micFromVar (mv));
    if (t.mics.empty()) {                                     // pre-`mics` takes: fall back to the v1 mirror
        const auto mic1 = v.getProperty ("mic", juce::var());
        if (mic1.isObject()) t.mics.push_back (micFromVar (mic1));
    }

    t.measured = measuredFromVar (v.getProperty ("measured", juce::var()));
    t.sweep    = sweepFromVar (v.getProperty ("sweep", juce::var()));

    if (auto* arr = v.getProperty ("mix", juce::var()).getArray())
        for (const auto& mv : *arr) t.mix.push_back (stripFromVar (mv));
    t.master = masterFromVar (v);                             // handles the master-object vs blend_* migration

    const auto mf = v.getProperty ("mono_filter", juce::var());
    if (mf.isObject()) t.monoFilter = stripFromVar (mf);

    return t;
}

// TakeMeta -> a take.json var: writes mics[] + the mic=mics[0] v1 mirror + measured (mics[0]'s numbers,
// same as the app writer). mono_filter is only written when present (the app only writes it for
// single-mic takes; here it's the caller's call, matching SessionStore::saveMix's contract).
inline juce::var takeToVar (const TakeMeta& t) {
    auto* o = new juce::DynamicObject();
    o->setProperty ("app", juce::String (t.app));
    if (! t.name.empty()) o->setProperty ("name", juce::String (t.name));   // additive: absent when unset
    o->setProperty ("timestamp", juce::String (t.timestamp));
    o->setProperty ("sample_rate", t.sampleRate);
    o->setProperty ("input_channel", t.inputChannel);
    o->setProperty ("interface", juce::String (t.interface_));

    auto* cab = new juce::DynamicObject();
    cab->setProperty ("instrument", juce::String (t.instrument));
    cab->setProperty ("enclosure", juce::String (t.enclosure));
    cab->setProperty ("model", juce::String (t.cabModel));
    cab->setProperty ("speaker", juce::String (t.speaker));
    cab->setProperty ("speaker_count", t.speakerCount);
    cab->setProperty ("speaker_size_in", juce::String (t.speakerSizeIn));
    cab->setProperty ("config", juce::String (t.speakerCount) + "x" + juce::String (t.speakerSizeIn));
    cab->setProperty ("tweeter", t.tweeter);
    cab->setProperty ("back", juce::String (t.back));
    o->setProperty ("cabinet", juce::var (cab));

    auto* ampO = new juce::DynamicObject();
    ampO->setProperty ("model", juce::String (t.ampModel));
    ampO->setProperty ("type", juce::String (t.ampType));
    o->setProperty ("amp", juce::var (ampO));

    o->setProperty ("room", juce::String (t.room));

    juce::Array<juce::var> micsArr;
    for (const auto& m : t.mics) micsArr.add (micToVar (m));
    o->setProperty ("mics", micsArr);
    o->setProperty ("mic", micsArr.isEmpty() ? juce::var() : micsArr[0]);   // v1-schema compat mirror

    o->setProperty ("measured", measuredToVar (t.measured));
    o->setProperty ("sweep", sweepToVar (t.sweep));

    juce::Array<juce::var> mix;
    for (const auto& s : t.mix) {
        auto* mo = new juce::DynamicObject();
        stripToVar (mo, s);
        mix.add (juce::var (mo));
    }
    o->setProperty ("mix", mix);

    { auto* mo = new juce::DynamicObject(); masterToVar (mo, t.master); o->setProperty ("master", juce::var (mo)); }

    if (t.monoFilter) {
        auto* mo = new juce::DynamicObject();
        stripToVar (mo, *t.monoFilter);
        o->setProperty ("mono_filter", juce::var (mo));
    }

    return juce::var (o);
}

inline SessionMeta sessionFromVar (const juce::var& v) {
    auto get = [&v] (const char* k) { return v.getProperty (k, "").toString().toStdString(); };
    SessionMeta s;
    s.instrument    = get ("instrument");
    s.enclosure     = get ("enclosure");
    s.cabModel      = get ("cab_model");
    s.speaker       = get ("speaker");
    s.speakerCount  = get ("speaker_count");
    s.speakerSizeIn = get ("speaker_size_in");
    s.tweeter       = get ("tweeter");
    s.back          = get ("back");
    s.amp           = get ("amp");
    s.ampType       = get ("amp_type");
    s.room          = get ("room");
    s.author        = get ("author");
    s.distUnit      = get ("dist_unit");
    return s;
}

inline juce::var sessionToVar (const SessionMeta& s) {
    auto* o = new juce::DynamicObject();
    o->setProperty ("instrument", juce::String (s.instrument));
    o->setProperty ("enclosure", juce::String (s.enclosure));
    o->setProperty ("cab_model", juce::String (s.cabModel));
    o->setProperty ("speaker", juce::String (s.speaker));
    o->setProperty ("speaker_count", juce::String (s.speakerCount));
    o->setProperty ("speaker_size_in", juce::String (s.speakerSizeIn));
    o->setProperty ("tweeter", juce::String (s.tweeter));
    o->setProperty ("back", juce::String (s.back));
    o->setProperty ("amp", juce::String (s.amp));
    o->setProperty ("amp_type", juce::String (s.ampType));
    o->setProperty ("room", juce::String (s.room));
    o->setProperty ("author", juce::String (s.author));
    o->setProperty ("dist_unit", juce::String (s.distUnit));
    return juce::var (o);
}

} // namespace ocap
