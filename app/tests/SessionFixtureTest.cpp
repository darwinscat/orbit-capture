// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// Legacy take.json fixture tests — pins the schema migrations (invert->phase_deg, fx_on/filter_on->
// hpf_on/lpf_on, v1 global blend_*->Master) + the mics[]-absent fallback bug fix, against real legacy
// files on disk (app/tests/fixtures/legacy_takes/*). De-monolith step 6.
#include <felitronics_test.h>
#include "model/SessionVar.h"

#include <juce_core/juce_core.h>

using felitronics::test::ok;
using felitronics::test::group;
using felitronics::test::approx;

#ifndef OC_FIXTURE_DIR
#error "OC_FIXTURE_DIR must be defined by CMake (see app/tests/CMakeLists.txt)"
#endif

namespace {
ocap::TakeMeta loadFixture (const char* name) {
    const auto f = juce::File (OC_FIXTURE_DIR).getChildFile (name).getChildFile ("take.json");
    return ocap::takeFromVar (juce::JSON::parse (f.loadFileAsString()));
}
} // namespace

int main() {
    std::printf ("orbitcapture session-fixture (legacy migration) tests\n");

    group ("v0_invert_fxon: invert->phaseDeg=180, fx_on->hpf+lpf on, mic->mics[0] fallback (N=1 not 0)");
    {
        const auto t = loadFixture ("v0_invert_fxon");
        ok (t.mics.size() == 1, "no mics[] array -> falls back to [mic], N=1 (not 0 — the bug this fixes)");
        ok (! t.mics.empty() && t.mics[0].model == "SM57", "fallback mic carries the real mic fields");
        ok (t.mix.size() == 1, "one mix strip");
        if (! t.mix.empty()) {
            approx (t.mix[0].phaseDeg, 180.0, 1e-9, "invert:true -> phaseDeg=180");
            ok (t.mix[0].hpf.on, "fx_on:true -> hpf.on");
            ok (t.mix[0].lpf.on, "fx_on:true -> lpf.on");
        }
    }

    group ("v1_blend_filteron: filter_on->hpf+lpf on, root blend_*->master");
    {
        const auto t = loadFixture ("v1_blend_filteron");
        ok (t.mics.size() == 1, "mics[] present, one mic (no fallback needed)");
        ok (t.mix.size() == 1, "one mix strip");
        if (! t.mix.empty()) {
            ok (t.mix[0].hpf.on, "filter_on:true -> hpf.on");
            ok (t.mix[0].lpf.on, "filter_on:true -> lpf.on");
        }
        ok (t.master.hpf.on, "blend_hpf_on -> master.hpf.on");
        approx (t.master.hpf.hz, 100.0, 1e-9, "blend_hpf_hz -> master.hpf.hz");
        ok (t.master.hpf.slopeDb == 24, "blend_hpf_slope -> master.hpf.slopeDb");
        ok (t.master.lpf.on, "blend_lpf_on -> master.lpf.on");
        approx (t.master.lpf.hz, 5000.0, 1e-9, "blend_lpf_hz -> master.lpf.hz");
        ok (t.master.lpf.slopeDb == 12, "blend_lpf_slope -> master.lpf.slopeDb");
    }

    group ("v2_master: modern master object read directly, mics[] used directly (N=2 not fallback)");
    {
        const auto t = loadFixture ("v2_master");
        ok (t.mics.size() == 2, "modern mics[] array used directly, N=2");
        if (t.mics.size() == 2) {
            ok (t.mics[0].model == "SM57", "mic0 parsed");
            ok (t.mics[1].model == "R121", "mic1 parsed");
            ok (t.mics[1].delaySamples == 3, "mic1 delay_samples parsed");
        }
        ok (t.mix.size() == 2, "two mix strips");
        if (t.mix.size() == 2) {
            approx (t.mix[1].phaseDeg, 180.0, 1e-9, "mix1 phase_deg read directly (modern key, no invert)");
            ok (t.mix[1].hpf.on, "mix1 hpf_on read directly");
        }
        approx (t.master.gainDb, -1.0, 1e-9, "master.gain_db read directly (no migration)");
        ok (t.master.hpf.on, "master.hpf_on read directly");
        approx (t.master.hpf.hz, 90.0, 1e-9, "master.hpf_hz read directly (not the v1 blend_* default)");
        ok (t.master.lpf.on, "master.lpf_on read directly");
        approx (t.master.lpf.hz, 9000.0, 1e-9, "master.lpf_hz read directly");
    }

    return felitronics::test::report();
}
