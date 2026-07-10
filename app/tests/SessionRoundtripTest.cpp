// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// Round-trip test for TakeMeta <-> take.json (juce_core tier). De-monolith step 6. Builds a TakeMeta in
// memory, serializes via takeToVar, re-parses the JSON STRING (not the in-memory var — a real disk
// round-trip) via takeFromVar, and asserts field-for-field identity.
#include <felitronics_test.h>
#include "model/SessionVar.h"

#include <juce_core/juce_core.h>

#include <string>

using felitronics::test::ok;
using felitronics::test::group;
using felitronics::test::approx;

namespace {

ocap::MicMeta makeMic (int idx) {
    ocap::MicMeta m;
    m.model         = "mic" + std::to_string (idx);
    m.location      = "cone";
    m.position      = "cone";
    m.axis          = idx % 2 == 0 ? "on-axis" : "off-axis";
    m.distanceInput = std::to_string (idx) + " in";
    m.distanceMm    = 25 * idx;
    m.inputChannel  = idx;
    m.slot          = idx - 1;
    m.gatePeakDbfs  = -6.0 - idx;
    m.gateSnr       = 10.0 + idx;
    m.snrDb         = 40.0 + idx;
    m.latencySamples = 128 + idx;
    m.delaySamples  = (std::size_t) (idx - 1) * 3;
    return m;
}

// Cutoffs deliberately stay far from the legacy off-sentinels (kHpfLo+0.5=20.5, kLpfHi-0.5=19999.5) —
// stripFromVar resets a cutoff AT/BELOW/ABOVE those back to the default, which would break round-trip
// identity for a value that happens to land there.
ocap::StripParams makeStrip (int idx) {
    ocap::StripParams p;
    p.gainDb   = -1.5 * idx;
    p.phaseDeg = idx % 2 == 0 ? 180.0 : 0.0;
    p.shiftMs  = 0.1 * idx;
    p.solo     = idx == 1;
    p.mute     = idx == 2;
    p.hpf = { idx % 2 == 0, 60.0 + idx * 10.0, 24 };
    p.lpf = { idx % 2 != 0, 5000.0 + idx * 100.0, 12 };
    return p;
}

ocap::TakeMeta makeTake (int nMics, bool withMono) {
    ocap::TakeMeta t;
    t.app = "OrbitCapture test"; t.timestamp = "20260710-120000"; t.interface_ = "Test Interface";
    t.sampleRate = 48000.0; t.inputChannel = 1;
    t.instrument = "guitar"; t.enclosure = "closed"; t.cabModel = "1x12"; t.speaker = "V30";
    t.speakerSizeIn = "12"; t.back = "closed"; t.ampModel = "5150"; t.ampType = "tube"; t.room = "live room";
    t.speakerCount = nMics > 1 ? 2 : 1;
    t.tweeter = nMics > 1;
    for (int i = 1; i <= nMics; ++i) t.mics.push_back (makeMic (i));
    t.measured = { -6.5, 11.0, 41.0, 0, 129, 24000 };
    t.sweep = { 80.0, 8000.0, 0.5, 0.1 };
    for (int i = 1; i <= nMics; ++i) t.mix.push_back (makeStrip (i));
    t.master = { -1.0, { true, 90.0, 24 }, { true, 9000.0, 12 } };
    if (withMono) t.monoFilter = makeStrip (99);
    return t;
}

void assertStrip (const ocap::StripParams& a, const ocap::StripParams& b, const std::string& tag) {
    approx (b.gainDb, a.gainDb, 1e-9, tag + " gainDb");
    approx (b.phaseDeg, a.phaseDeg, 1e-9, tag + " phaseDeg");
    approx (b.shiftMs, a.shiftMs, 1e-9, tag + " shiftMs");
    ok (b.solo == a.solo, tag + " solo");
    ok (b.mute == a.mute, tag + " mute");
    ok (b.hpf.on == a.hpf.on, tag + " hpf.on");
    approx (b.hpf.hz, a.hpf.hz, 1e-9, tag + " hpf.hz");
    ok (b.hpf.slopeDb == a.hpf.slopeDb, tag + " hpf.slopeDb");
    ok (b.lpf.on == a.lpf.on, tag + " lpf.on");
    approx (b.lpf.hz, a.lpf.hz, 1e-9, tag + " lpf.hz");
    ok (b.lpf.slopeDb == a.lpf.slopeDb, tag + " lpf.slopeDb");
}

void assertMaster (const ocap::MasterParams& a, const ocap::MasterParams& b, const std::string& tag) {
    approx (b.gainDb, a.gainDb, 1e-9, tag + " gainDb");
    ok (b.hpf.on == a.hpf.on, tag + " hpf.on");
    approx (b.hpf.hz, a.hpf.hz, 1e-9, tag + " hpf.hz");
    ok (b.hpf.slopeDb == a.hpf.slopeDb, tag + " hpf.slopeDb");
    ok (b.lpf.on == a.lpf.on, tag + " lpf.on");
    approx (b.lpf.hz, a.lpf.hz, 1e-9, tag + " lpf.hz");
    ok (b.lpf.slopeDb == a.lpf.slopeDb, tag + " lpf.slopeDb");
}

void roundTripCheck (int nMics, bool withMono) {
    const auto tag = "n=" + std::to_string (nMics) + (withMono ? " mono" : " nomono");
    const auto original = makeTake (nMics, withMono);
    const auto js = juce::JSON::toString (ocap::takeToVar (original));
    const auto parsed = ocap::takeFromVar (juce::JSON::parse (js));

    ok (parsed.app == original.app, tag + " app");
    ok (parsed.timestamp == original.timestamp, tag + " timestamp");
    ok (parsed.interface_ == original.interface_, tag + " interface_");
    approx (parsed.sampleRate, original.sampleRate, 1e-9, tag + " sampleRate");
    ok (parsed.inputChannel == original.inputChannel, tag + " inputChannel");
    ok (parsed.instrument == original.instrument, tag + " instrument");
    ok (parsed.enclosure == original.enclosure, tag + " enclosure");
    ok (parsed.cabModel == original.cabModel, tag + " cabModel");
    ok (parsed.speaker == original.speaker, tag + " speaker");
    ok (parsed.speakerSizeIn == original.speakerSizeIn, tag + " speakerSizeIn");
    ok (parsed.back == original.back, tag + " back");
    ok (parsed.ampModel == original.ampModel, tag + " ampModel");
    ok (parsed.ampType == original.ampType, tag + " ampType");
    ok (parsed.room == original.room, tag + " room");
    ok (parsed.speakerCount == original.speakerCount, tag + " speakerCount");
    ok (parsed.tweeter == original.tweeter, tag + " tweeter");

    ok (parsed.mics.size() == original.mics.size(), tag + " mic count");
    for (std::size_t i = 0; i < original.mics.size() && i < parsed.mics.size(); ++i) {
        const auto& a = original.mics[i]; const auto& b = parsed.mics[i];
        const auto mtag = tag + " mic" + std::to_string (i);
        ok (b.model == a.model, mtag + " model");
        ok (b.location == a.location, mtag + " location");
        ok (b.position == a.position, mtag + " position");
        ok (b.axis == a.axis, mtag + " axis");
        ok (b.distanceInput == a.distanceInput, mtag + " distanceInput");
        ok (b.distanceMm == a.distanceMm, mtag + " distanceMm");
        ok (b.inputChannel == a.inputChannel, mtag + " inputChannel");
        ok (b.slot == a.slot, mtag + " slot");
        approx (b.gatePeakDbfs, a.gatePeakDbfs, 1e-9, mtag + " gatePeakDbfs");
        approx (b.gateSnr, a.gateSnr, 1e-9, mtag + " gateSnr");
        approx (b.snrDb, a.snrDb, 1e-9, mtag + " snrDb");
        ok (b.latencySamples == a.latencySamples, mtag + " latencySamples");
        ok (b.delaySamples == a.delaySamples, mtag + " delaySamples");
    }

    approx (parsed.measured.gatePeakDbfs, original.measured.gatePeakDbfs, 1e-9, tag + " measured.gatePeakDbfs");
    approx (parsed.measured.gateSnr, original.measured.gateSnr, 1e-9, tag + " measured.gateSnr");
    approx (parsed.measured.snrDb, original.measured.snrDb, 1e-9, tag + " measured.snrDb");
    ok (parsed.measured.clipRun == original.measured.clipRun, tag + " measured.clipRun");
    ok (parsed.measured.latencySamples == original.measured.latencySamples, tag + " measured.latencySamples");
    ok (parsed.measured.irLen == original.measured.irLen, tag + " measured.irLen");

    approx (parsed.sweep.f1, original.sweep.f1, 1e-9, tag + " sweep.f1");
    approx (parsed.sweep.f2, original.sweep.f2, 1e-9, tag + " sweep.f2");
    approx (parsed.sweep.dur, original.sweep.dur, 1e-9, tag + " sweep.dur");
    approx (parsed.sweep.tail, original.sweep.tail, 1e-9, tag + " sweep.tail");

    ok (parsed.mix.size() == original.mix.size(), tag + " mix count");
    for (std::size_t i = 0; i < original.mix.size() && i < parsed.mix.size(); ++i)
        assertStrip (original.mix[i], parsed.mix[i], tag + " mix" + std::to_string (i));

    assertMaster (original.master, parsed.master, tag + " master");

    ok (parsed.monoFilter.has_value() == original.monoFilter.has_value(), tag + " monoFilter presence");
    if (original.monoFilter && parsed.monoFilter)
        assertStrip (*original.monoFilter, *parsed.monoFilter, tag + " monoFilter");
}

} // namespace

int main() {
    std::printf ("orbitcapture session-roundtrip tests\n");

    group ("1-mic take, no mono_filter");
    roundTripCheck (1, false);

    group ("1-mic take, WITH mono_filter");
    roundTripCheck (1, true);

    group ("4-mic take, no mono_filter");
    roundTripCheck (4, false);

    group ("4-mic take, WITH mono_filter");
    roundTripCheck (4, true);

    group ("mic == mics[0] mirror is written (post-JSON-string round-trip)");
    {
        const auto v = ocap::takeToVar (makeTake (2, false));
        const auto reparsed = juce::JSON::parse (juce::JSON::toString (v));
        auto* arr = reparsed.getProperty ("mics", juce::var()).getArray();
        ok (arr != nullptr && arr->size() == 2, "mics array present with 2 entries");
        if (arr != nullptr && ! arr->isEmpty()) {
            const auto mic0Json = juce::JSON::toString ((*arr)[0]);
            const auto micJson  = juce::JSON::toString (reparsed.getProperty ("mic", juce::var()));
            ok (mic0Json == micJson, "mic mirror equals mics[0] after JSON round-trip");
        }
    }

    return felitronics::test::report();
}
