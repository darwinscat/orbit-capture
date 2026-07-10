// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// SessionStore file-I/O tests: create/save/scan/load a session+take on disk, verify sample-exact IR
// round-trip + the read-modify-write contract for saveMix (unknown/future keys must survive). De-monolith
// step 6. juce_core tier — a real temp dir stands in for sessionsRoot().
#include <felitronics_test.h>
#include "persist/SessionStore.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <optional>
#include <vector>

using felitronics::test::ok;
using felitronics::test::group;
using felitronics::test::approx;

namespace {
std::vector<double> synth (std::size_t n, double seed) {
    std::vector<double> v (n);
    for (std::size_t i = 0; i < n; ++i) v[i] = std::sin (seed + 0.01 * (double) i) * 0.5;
    return v;
}
} // namespace

int main() {
    std::printf ("orbitcapture session-store tests\n");

    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("oc_session_store_test_" + juce::String ((int) juce::Time::currentTimeMillis()));
    root.deleteRecursively();
    ocap::SessionStore store (root);

    juce::File takeDir;
    const auto ir1 = synth (2400, 2.0), ir2 = synth (2400, 3.0);

    group ("create session -> save 2-mic take -> scan -> load, sample-exact IRs + fields");
    {
        const auto sessionDir = store.createSession ("unit test cab");
        ok (sessionDir.isDirectory(), "session dir created");

        ocap::TakeMeta take;
        take.app = "OrbitCapture test"; take.timestamp = "20260710-130000"; take.interface_ = "Test IF";
        take.sampleRate = 48000.0; take.inputChannel = 1;
        take.instrument = "guitar"; take.cabModel = "1x12"; take.speaker = "V30"; take.room = "live";
        take.speakerCount = 2; take.tweeter = true; take.speakerSizeIn = "12"; take.back = "closed";
        take.ampModel = "5150"; take.ampType = "tube"; take.enclosure = "closed";

        ocap::MicMeta m1; m1.model = "SM57"; m1.inputChannel = 1; m1.slot = 0;
        ocap::MicMeta m2; m2.model = "R121"; m2.inputChannel = 2; m2.slot = 1;
        take.mics = { m1, m2 };
        take.measured = { -6.0, 10.0, 40.0, 0, 128, 4800 };
        take.sweep = { 80.0, 8000.0, 0.5, 0.1 };
        take.mix.push_back (ocap::StripParams{});
        take.mix.push_back (ocap::StripParams{});
        take.master = ocap::MasterParams{};

        const auto raw1 = synth (4800, 0.0), raw2 = synth (4800, 1.0);

        takeDir = store.saveTake (sessionDir, take, { raw1, raw2 }, { ir1, ir2 }, 48000.0);
        ok (takeDir.getFileName() == "take01", "first take numbered take01");
        ok (takeDir.getChildFile ("raw_mic1.wav").existsAsFile(), "raw_mic1.wav written (N>1 suffix rule)");
        ok (takeDir.getChildFile ("raw_mic2.wav").existsAsFile(), "raw_mic2.wav written");
        ok (takeDir.getChildFile ("ir_mic1.wav").existsAsFile(), "ir_mic1.wav written");
        ok (takeDir.getChildFile ("ir_mic2.wav").existsAsFile(), "ir_mic2.wav written");
        ok (takeDir.getChildFile ("take.json").existsAsFile(), "take.json written");

        const auto takes = store.scanTakes (sessionDir);
        ok (takes.size() == 1 && takes[0] == takeDir, "scanTakes finds the one take dir");

        const auto loaded = store.loadTake (takeDir);
        ok (loaded.take.mics.size() == 2, "loaded TakeMeta has 2 mics");
        if (loaded.take.mics.size() == 2) {
            ok (loaded.take.mics[0].model == "SM57", "mic0 model survives");
            ok (loaded.take.mics[1].model == "R121", "mic1 model survives");
        }
        ok (loaded.take.cabModel == "1x12", "TakeMeta scalar field (cabModel) survives");
        approx (loaded.sampleRate, 48000.0, 1e-9, "sample rate survives");
        ok (loaded.irs.size() == 2, "two IRs loaded");
        if (loaded.irs.size() == 2) {
            ok (loaded.irs[0].size() == ir1.size() && loaded.irs[1].size() == ir2.size(), "IR lengths match");
            bool sampleExact = true;
            for (std::size_t i = 0; i < ir1.size() && sampleExact; ++i)
                if (std::fabs ((float) ir1[i] - (float) loaded.irs[0][i]) > 1e-6f) sampleExact = false;
            for (std::size_t i = 0; i < ir2.size() && sampleExact; ++i)
                if (std::fabs ((float) ir2[i] - (float) loaded.irs[1][i]) > 1e-6f) sampleExact = false;
            ok (sampleExact, "IR samples survive the WAV round-trip exactly (32-bit float, no lossy re-quant)");
        }
    }

    group ("saveMix is read-modify-write: an injected future_key survives");
    {
        // Simulate a newer schema version having added a field this code doesn't model yet.
        const auto f = takeDir.getChildFile ("take.json");
        auto v = juce::JSON::parse (f.loadFileAsString());
        if (auto* o = v.getDynamicObject()) o->setProperty ("future_key", 123);
        f.replaceWithText (juce::JSON::toString (v));

        std::vector<ocap::StripParams> mix (2);
        mix[0].gainDb = -3.0; mix[1].gainDb = 2.0;
        ocap::MasterParams master; master.gainDb = -0.5;
        store.saveMix (takeDir, mix, master, std::nullopt);

        const auto v2 = juce::JSON::parse (f.loadFileAsString());
        ok ((int) v2.getProperty ("future_key", 0) == 123, "future_key survived the RMW");

        const auto reloaded = store.loadTake (takeDir);
        ok (reloaded.take.mix.size() == 2, "mix still 2 strips after saveMix");
        if (reloaded.take.mix.size() == 2) {
            approx (reloaded.take.mix[0].gainDb, -3.0, 1e-9, "mix[0].gainDb updated by saveMix");
            approx (reloaded.take.mix[1].gainDb, 2.0, 1e-9, "mix[1].gainDb updated by saveMix");
        }
        approx (reloaded.take.master.gainDb, -0.5, 1e-9, "master.gainDb updated by saveMix");
        ok (! reloaded.take.monoFilter.has_value(), "mono_filter not written when nullopt passed");
        ok (reloaded.take.mics.size() == 2, "mics[] untouched by saveMix (RMW touches only mix/master/mono_filter)");
    }

    group ("saveMix writes mono_filter when provided");
    {
        ocap::StripParams mono; mono.gainDb = 1.0; mono.hpf = { true, 65.0, 24 };
        store.saveMix (takeDir, { ocap::StripParams{}, ocap::StripParams{} }, ocap::MasterParams{}, mono);
        const auto reloaded = store.loadTake (takeDir);
        ok (reloaded.take.monoFilter.has_value(), "mono_filter present after saveMix(..., mono)");
        if (reloaded.take.monoFilter) {
            approx (reloaded.take.monoFilter->gainDb, 1.0, 1e-9, "mono_filter.gainDb survives");
            ok (reloaded.take.monoFilter->hpf.on, "mono_filter.hpf.on survives");
        }
    }

    root.deleteRecursively();
    return felitronics::test::report();
}
