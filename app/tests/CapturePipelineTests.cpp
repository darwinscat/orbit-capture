// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// Headless test for the post-capture pipeline (de-monolith step 5). JUCE-free. Synthesizes each mic's
// recording as sweep⊛knownIR at a distinct onset, then checks the pipeline recovers the IRs, preserves
// the inter-mic delays (the common time reference), and rejects the whole set when one mic fails the gate.
#include <felitronics_test.h>
#include "core/CapturePipeline.h"
#include "oc/sweep.hpp"
#include "oc/fft.hpp"     // oc::convolve

#include <cmath>
#include <vector>

using felitronics::test::ok;
using felitronics::test::group;

int main()
{
    std::printf ("orbitcapture capture-pipeline tests\n");
    const double sr = 48000.0;
    oc::SweepSpec spec; spec.sr = sr; spec.f1 = 100.0; spec.f2 = 8000.0; spec.dur = 0.5; spec.tail = 0.1;
    const oc::Sweep sw = oc::make_sweep (spec);

    // Two mics: single-tap IRs at distinct positions (away from the onset-search boundary) — mic2's tap
    // is 10 samples later than mic1's → a 10-sample inter-mic delay the common reference must preserve.
    auto recFor = [&] (int pos) {
        std::vector<double> ir ((std::size_t) pos + 1, 0.0); ir[(std::size_t) pos] = 1.0;
        return oc::convolve (sw.x, ir);            // the "recording": the played sweep through that IR
    };

    group ("recovers IRs + preserves inter-mic delay");
    {
        std::vector<std::vector<double>> recs { recFor (20), recFor (30) };
        const ocap::CaptureResult r = ocap::runCapturePipeline (recs, sw, sr);
        ok (r.ok, "clean 2-mic set passes");
        ok (r.irs.size() == 2 && ! r.irs[0].empty(), "two IRs out");
        ok (r.delaySamples.size() == 2 && r.delaySamples[0] == 0 && r.delaySamples[1] == 10,
            "inter-mic delay preserved exactly (mic2 = +10 samples vs the shared reference)");
        // Both taps back off preRoll equally; mic2's sits exactly 10 later than mic1's. Shared 0.98 normalize.
        double pk0 = 0.0, pk1 = 0.0; std::size_t at0 = 0, at1 = 0;
        for (std::size_t i = 0; i < r.irs[0].size(); ++i) { if (std::fabs (r.irs[0][i]) > pk0) { pk0 = std::fabs (r.irs[0][i]); at0 = i; } }
        for (std::size_t i = 0; i < r.irs[1].size(); ++i) { if (std::fabs (r.irs[1][i]) > pk1) { pk1 = std::fabs (r.irs[1][i]); at1 = i; } }
        ok (at1 == at0 + 10, "mic2 tap sits exactly 10 samples after mic1 in the aligned IRs");
        ok (std::fabs (std::max (pk0, pk1) - 0.98) < 1e-6, "shared normalize → set peak == 0.98");
    }

    group ("whole-set reject on one bad mic");
    {
        std::vector<std::vector<double>> recs { recFor (20), std::vector<double> (recFor (20).size(), 0.0) };  // mic2 = silence
        const ocap::CaptureResult r = ocap::runCapturePipeline (recs, sw, sr);
        ok (! r.ok, "one gate-fail rejects the WHOLE set");
        ok (r.gates.size() == 2, "both gates still reported (for the message)");
        ok (r.irs.empty(), "no IRs on reject");
    }

    return felitronics::test::report();
}
