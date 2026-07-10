// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// Writer-parity golden test (de-monolith step 6d — the persistence rewire's guard). Proves
// SessionVar::takeToVar is schema-compatible with the take.json Main.cpp's saveTakeToSession
// (~line 1599-1659) actually writes on disk today, so rewiring Main.cpp onto SessionStore does not
// silently change users' real take.json files. Method: hand-author, INLINE, the exact JSON that
// function produces for an equivalent capture (verified against Main.cpp's source + real on-disk
// take.json files — see below), then deep-compare it (key-set + value, order-independent) against
// juce::JSON::parse(juce::JSON::toString(takeToVar(take))) for the matching TakeMeta.
//
// One deliberate, already-shipped divergence from the OLD minimal capture-time writer: a genuinely
// FRESH (never mix-edited) take.json on disk has NO "master" key and a "mix"[] whose entries carry only
// {gain_db, phase_deg} — Main.cpp only starts writing "master" / the full per-strip schema once the user
// first touches a mix control (saveMixToTake, a read-modify-write). takeToVar, however, ALWAYS writes a
// full-fidelity "master" + full per-strip mix schema (already locked in + tested field-for-field by
// SessionRoundtripTest.cpp, ported in an earlier de-monolith step). That is a strictly ADDITIVE,
// backward/forward-compatible enrichment (old app builds ignore unknown keys; masterFromVar/stripFromVar
// already default any keys that are missing) — consistent with this codebase's existing "additive field"
// precedent (see Main.cpp's "interface" / "mics[]" comments). So this test targets the fields that are
// genuinely SHARED between the two writers (app/timestamp/sample_rate/input_channel/interface/cabinet/
// amp/room/mics[]/mic/measured/sweep) for byte-for-byte parity, and mix[]/master/mono_filter against
// takeToVar's own (already-correct) stripToVar/masterToVar schema.
#include <felitronics_test.h>
#include "model/SessionVar.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <string>

using felitronics::test::ok;
using felitronics::test::group;

namespace {

// A 2-mic, fully-populated, realistic TakeMeta — the in-memory equivalent of what the rewired
// saveTakeToSession will build from the UI. Field-for-field, this mirrors Main.cpp:1599-1659's reads.
ocap::TakeMeta makeFixtureTake() {
    ocap::TakeMeta t;
    t.app = "OrbitCapture 0.1";
    t.timestamp = "20260710-153000";
    t.interface_ = "Focusrite Scarlett 2i2";
    t.sampleRate = 48000.0;
    t.inputChannel = 1;                                    // rtChans[0] + 1

    t.instrument = "guitar"; t.enclosure = "closed back";
    t.cabModel = "Ornge PPC212V"; t.speaker = "Celestion V30";
    t.speakerCount = 2; t.speakerSizeIn = "12";              // countBox/sizeBox text -> "2x12" config
    t.tweeter = true; t.back = "closed";
    t.ampModel = "5150 III"; t.ampType = "tube";
    t.room = "live room";

    ocap::MicMeta m0;
    m0.model = "Shure SM57"; m0.location = "grille"; m0.position = "Cap Edge"; m0.axis = "On-axis";
    m0.distanceInput = "1 in"; m0.distanceMm = 25; m0.inputChannel = 1;
    m0.gatePeakDbfs = -6.2; m0.gateSnr = 12500.0; m0.snrDb = 41.5;
    m0.latencySamples = 128; m0.delaySamples = 0; m0.slot = 0;

    ocap::MicMeta m1;
    m1.model = "Royer R121"; m1.location = "cone"; m1.position = "cone"; m1.axis = "Off-axis";  // location != "grille" -> position falls back to location text
    m1.distanceInput = "2 in"; m1.distanceMm = 51; m1.inputChannel = 2;
    m1.gatePeakDbfs = -8.1; m1.gateSnr = 9800.0; m1.snrDb = 38.2;
    m1.latencySamples = 130; m1.delaySamples = 3; m1.slot = 1;

    t.mics = { m0, m1 };

    // measured{} == mics[0]'s numbers (v1 compat), same as saveTakeToSession's gs[0]/cap.latency[0].
    t.measured = { -6.2, 12500.0, 41.5, 0, 128, 24000 };
    t.sweep = { 20.0, 20000.0, 5.0, 1.0 };

    // mix[] / master — richer-than-capture-time-minimal on purpose (see file banner): takeToVar's
    // already-shipped, already-tested full-fidelity schema, not the transient 2-key capture stub.
    ocap::StripParams s0;
    s0.gainDb = -3.0; s0.phaseDeg = 0.0; s0.shiftMs = 0.0; s0.solo = false; s0.mute = false;
    s0.hpf = { true, 90.0, 24 }; s0.lpf = { false, 8000.0, 12 };
    ocap::StripParams s1;
    s1.gainDb = 1.5; s1.phaseDeg = 180.0; s1.shiftMs = 0.3; s1.solo = false; s1.mute = false;
    s1.hpf = { false, 80.0, 24 }; s1.lpf = { true, 6000.0, 12 };
    t.mix = { s0, s1 };

    t.master.gainDb = -1.0;
    t.master.hpf = { true, 100.0, 24 };
    t.master.lpf = { true, 9000.0, 12 };

    // 2 mics -> mono_filter is never written (Main.cpp only writes it when mixRows.size() < 2).
    t.monoFilter = std::nullopt;

    return t;
}

// The EXACT take.json Main.cpp's saveTakeToSession (lines 1599-1659) would write for the capture that
// makeFixtureTake() models — hand-authored straight from that function's source, cross-checked against
// real on-disk take.json files (~/Library/OrbitCapture/sessions/*/take*/take.json). Key content beyond
// the shared fields (mix[] full schema, master) matches takeToVar's own stripToVar/masterToVar contract
// (see file banner) rather than the OLD minimal capture-time stub, which is the sanctioned enrichment.
const char* kGoldenTakeJson = R"JSON(
{
  "app": "OrbitCapture 0.1",
  "timestamp": "20260710-153000",
  "sample_rate": 48000.0,
  "input_channel": 1,
  "interface": "Focusrite Scarlett 2i2",
  "cabinet": {
    "instrument": "guitar",
    "enclosure": "closed back",
    "model": "Ornge PPC212V",
    "speaker": "Celestion V30",
    "speaker_count": 2,
    "speaker_size_in": "12",
    "config": "2x12",
    "tweeter": true,
    "back": "closed"
  },
  "amp": { "model": "5150 III", "type": "tube" },
  "room": "live room",
  "mics": [
    {
      "model": "Shure SM57", "location": "grille", "position": "Cap Edge", "axis": "On-axis",
      "distance_mm": 25, "distance_input": "1 in", "input_channel": 1,
      "gate_peak_dbfs": -6.2, "gate_snr": 12500.0, "snr_db": 41.5,
      "latency_samples": 128, "delay_samples": 0, "slot": 0
    },
    {
      "model": "Royer R121", "location": "cone", "position": "cone", "axis": "Off-axis",
      "distance_mm": 51, "distance_input": "2 in", "input_channel": 2,
      "gate_peak_dbfs": -8.1, "gate_snr": 9800.0, "snr_db": 38.2,
      "latency_samples": 130, "delay_samples": 3, "slot": 1
    }
  ],
  "mic": {
    "model": "Shure SM57", "location": "grille", "position": "Cap Edge", "axis": "On-axis",
    "distance_mm": 25, "distance_input": "1 in", "input_channel": 1,
    "gate_peak_dbfs": -6.2, "gate_snr": 12500.0, "snr_db": 41.5,
    "latency_samples": 128, "delay_samples": 0, "slot": 0
  },
  "measured": {
    "gate_peak_dbfs": -6.2, "gate_snr": 12500.0, "snr_db": 41.5,
    "clip_run": 0, "latency_samples": 128, "ir_len": 24000
  },
  "sweep": { "f1": 20.0, "f2": 20000.0, "dur": 5.0, "tail": 1.0 },
  "mix": [
    {
      "gain_db": -3.0, "hpf_on": true, "lpf_on": false, "hpf_hz": 90.0, "hpf_slope": 24,
      "lpf_hz": 8000.0, "lpf_slope": 12, "phase_deg": 0.0, "shift_ms": 0.0, "solo": false, "mute": false
    },
    {
      "gain_db": 1.5, "hpf_on": false, "lpf_on": true, "hpf_hz": 80.0, "hpf_slope": 24,
      "lpf_hz": 6000.0, "lpf_slope": 12, "phase_deg": 180.0, "shift_ms": 0.3, "solo": false, "mute": false
    }
  ],
  "master": {
    "gain_db": -1.0, "hpf_on": true, "lpf_on": true, "hpf_hz": 100.0, "hpf_slope": 24,
    "lpf_hz": 9000.0, "lpf_slope": 12
  }
}
)JSON";

// Order-independent deep-equal over parsed juce::var trees. Numeric leaves compare by VALUE (int vs
// double representation is not schema-significant — JSON has one number type; a reader casts either way).
bool deepEqual (const juce::var& a, const juce::var& b, juce::String& why) {
    // NB juce::var's Array variant sets BOTH isObject()==true AND isArray()==true (juce_Variant.cpp's
    // VariantType_Array ctor), so isArray() (the more specific case) MUST be checked before isObject().
    if (a.isArray() || b.isArray()) {
        auto* aa = a.getArray(); auto* ab = b.getArray();
        if (aa == nullptr || ab == nullptr) { why = "one side is not an array"; return false; }
        if (aa->size() != ab->size()) {
            why = "array size differs (" + juce::String (aa->size()) + " vs " + juce::String (ab->size()) + ")";
            return false;
        }
        for (int i = 0; i < aa->size(); ++i) {
            juce::String sub;
            if (! deepEqual ((*aa)[i], (*ab)[i], sub)) { why = "[" + juce::String (i) + "]: " + sub; return false; }
        }
        return true;
    }
    if (a.isObject() || b.isObject()) {
        auto* oa = a.getDynamicObject(); auto* ob = b.getDynamicObject();
        if (oa == nullptr || ob == nullptr) { why = "one side is not an object"; return false; }
        const auto& propsA = oa->getProperties();
        const auto& propsB = ob->getProperties();
        if (propsA.size() != propsB.size()) {
            juce::StringArray namesA, namesB;
            for (int i = 0; i < propsA.size(); ++i) namesA.add (propsA.getName (i).toString());
            for (int i = 0; i < propsB.size(); ++i) namesB.add (propsB.getName (i).toString());
            why = "key count differs (" + juce::String (propsA.size()) + " vs " + juce::String (propsB.size())
                + "): golden=[" + namesA.joinIntoString (",") + "] actual=[" + namesB.joinIntoString (",") + "]";
            return false;
        }
        for (int i = 0; i < propsA.size(); ++i) {
            const auto key = propsA.getName (i);
            if (! ob->hasProperty (key)) { why = "actual missing key \"" + key.toString() + "\""; return false; }
            juce::String sub;
            if (! deepEqual (propsA.getValueAt (i), ob->getProperty (key), sub)) {
                why = "\"" + key.toString() + "\": " + sub;
                return false;
            }
        }
        return true;
    }
    if (a.isBool() || b.isBool()) {
        if (! a.isBool() || ! b.isBool() || (bool) a != (bool) b) {
            why = "bool " + a.toString() + " != " + b.toString();
            return false;
        }
        return true;
    }
    if (a.isDouble() || b.isDouble() || a.isInt() || b.isInt() || a.isInt64() || b.isInt64()) {
        const double da = (double) a, db = (double) b;
        if (std::fabs (da - db) > 1e-9) { why = "number " + a.toString() + " != " + b.toString(); return false; }
        return true;
    }
    if (a.toString() != b.toString()) { why = "\"" + a.toString() + "\" != \"" + b.toString() + "\""; return false; }
    return true;
}

} // namespace

int main() {
    std::printf ("orbitcapture session-writer-parity test\n");

    group ("takeToVar(fixture) deep-equals the hand-authored take.json Main.cpp's saveTakeToSession writes");
    {
        const auto take = makeFixtureTake();

        const auto golden = juce::JSON::parse (juce::String (kGoldenTakeJson));
        ok (golden != juce::var(), "golden JSON string parses");

        const auto actualJson = juce::JSON::toString (ocap::takeToVar (take));
        const auto actual = juce::JSON::parse (actualJson);
        ok (actual != juce::var(), "takeToVar output re-parses");

        juce::String why;
        const bool eq = deepEqual (golden, actual, why);
        ok (eq, eq ? "takeToVar(fixture) matches the golden take.json exactly"
                   : ("takeToVar(fixture) MISMATCH: " + why).toStdString());

        // mic == mics[0] mirror, same invariant Main.cpp's writer + SessionRoundtripTest.cpp assert.
        if (auto* arr = actual.getProperty ("mics", juce::var()).getArray()) {
            juce::String subWhy;
            ok (! arr->isEmpty() && deepEqual ((*arr)[0], actual.getProperty ("mic", juce::var()), subWhy),
                "mic mirrors mics[0]");
        } else {
            ok (false, "mics[] present");
        }
    }

    return felitronics::test::report();
}
