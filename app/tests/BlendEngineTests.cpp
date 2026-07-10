// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// Headless tests for the mic-blend engine (de-monolith step 3). JUCE-free. These are INDEPENDENT
// property checks (not a re-run of the engine): default params must equal a plain sum that bypasses the
// kernels; polarity must cancel; solo/mute/gain/master semantics; "filter parked at its extreme ≡ off".
#include <felitronics_test.h>
#include "model/MixModel.h"
#include "core/BlendEngine.h"

#include <cmath>
#include <vector>

using felitronics::test::ok;
using felitronics::test::approx;
using felitronics::test::group;

namespace
{
std::vector<float> ramp (std::size_t n, float scale = 1.0f)
{
    std::vector<float> x (n);
    for (std::size_t i = 0; i < n; ++i) x[i] = scale * std::sin (0.03f * (float) i + 0.5f);
    return x;
}
double maxAbsDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    double d = 0.0; for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) d = std::max (d, (double) std::fabs (a[i] - b[i]));
    return d;
}
}

int main()
{
    std::printf ("orbitcapture blend-engine tests\n");
    const double sr = 48000.0;
    using namespace ocap;

    const auto ir = ramp (2048);
    const MasterParams flat;   // gain 0 dB, filters off → a no-op master

    // Default strips + flat master == a plain Σ that never touches the kernels (independent).
    group ("default params → plain sum");
    {
        std::vector<std::vector<float>> irs { ir, ir, ir };
        std::vector<StripParams> strips (3);                 // all defaults: 0 dB, 0°, 0 ms, filters off
        const auto b = blendIrs (irs, strips, flat, sr);
        std::vector<float> want (ir.size());
        for (std::size_t i = 0; i < ir.size(); ++i) want[i] = 3.0f * ir[i];
        ok (maxAbsDiff (b, want) < 1e-5, "3 identical mics @ default → 3×ir (no kernel ops)");
    }

    // Polarity: two identical mics, one rotated 180° → cancellation.
    group ("phase 180° cancels");
    {
        std::vector<std::vector<float>> irs { ir, ir };
        std::vector<StripParams> strips (2);
        strips[1].phaseDeg = 180.0;
        const auto b = blendIrs (irs, strips, flat, sr);
        double pk = 0.0; for (float v : b) pk = std::max (pk, (double) std::fabs (v));
        ok (pk < 1e-3, "ir + (−ir) ≈ 0");
    }

    // Mute drops a strip; solo overrides mute.
    group ("solo / mute");
    {
        std::vector<std::vector<float>> irs { ir, ir };
        std::vector<StripParams> strips (2);
        strips[1].mute = true;
        ok (maxAbsDiff (blendIrs (irs, strips, flat, sr), ir) < 1e-5, "muted strip drops out → other mic alone");

        std::vector<std::vector<float>> irs3 { ir, ir, ir };
        std::vector<StripParams> s3 (3);
        s3[0].solo = true; s3[1].mute = true;                 // solo on 0; mute on 1 ignored (solo active)
        const auto b = blendIrs (irs3, s3, flat, sr);
        ok (maxAbsDiff (b, ir) < 1e-5, "any solo → only soloed audible (mute irrelevant)");
        ok (channelAudible (s3, 0) && ! channelAudible (s3, 1) && ! channelAudible (s3, 2), "audible mask: solo overrides");
    }

    // Gain: −20 dB → ×0.1 per sample (single mic, flat master).
    group ("gain scaling");
    {
        std::vector<std::vector<float>> irs { ir };
        std::vector<StripParams> strips (1);
        strips[0].gainDb = -20.0;
        const auto b = blendIrs (irs, strips, flat, sr);
        std::vector<float> want (ir.size()); for (std::size_t i = 0; i < ir.size(); ++i) want[i] = 0.1f * ir[i];
        ok (maxAbsDiff (b, want) < 1e-5, "gainDb=−20 → 0.1×ir");
    }

    // Master: −6 dB, filters off → whole blend ×10^(−6/20).
    group ("master gain");
    {
        std::vector<std::vector<float>> irs { ir };
        std::vector<StripParams> strips (1);
        MasterParams m; m.gainDb = -6.0;
        const auto post = blendIrs (irs, strips, m, sr, /*applyMaster*/ true);
        const auto pre  = blendIrs (irs, strips, m, sr, /*applyMaster*/ false);
        const double gm = std::pow (10.0, -6.0 / 20.0);
        std::vector<float> want (ir.size()); for (std::size_t i = 0; i < ir.size(); ++i) want[i] = (float) (gm * ir[i]);
        ok (maxAbsDiff (post, want) < 1e-5, "master −6 dB scales the whole blend");
        ok (maxAbsDiff (pre, ir) < 1e-5, "applyMaster=false → pre-master (unscaled)");
    }

    // "Filter parked at its extreme ≡ off" — hpf.on but hz at kHpfLo → no filtering.
    group ("parked filter ≡ off");
    {
        std::vector<std::vector<float>> irs { ir };
        std::vector<StripParams> parked (1), off (1);
        parked[0].hpf = { true, kHpfLo, 24 };                 // enabled but parked at the low extreme
        const auto bParked = blendIrs (irs, parked, flat, sr);
        const auto bOff    = blendIrs (irs, off,    flat, sr);
        ok (maxAbsDiff (bParked, bOff) < 1e-9, "hpf parked at kHpfLo bypasses (== filter off)");
        ok (hpActiveSlope (parked[0].hpf) == 0, "hpActiveSlope=0 when parked");
    }

    // Degenerate sets return empty.
    group ("degenerate → empty");
    {
        std::vector<std::vector<float>> none;
        std::vector<StripParams> s2 (2);
        ok (blendIrs (none, {}, flat, sr).empty(), "empty irs → {}");
        std::vector<std::vector<float>> one { ir };
        ok (blendIrs (one, s2, flat, sr).empty(), "size mismatch (1 ir, 2 strips) → {}");
    }

    return felitronics::test::report();
}
