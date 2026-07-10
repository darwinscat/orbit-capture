// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// End-to-end tests for the Auto button's maths (core/AutoAlign.h): the suggested shift/polarity,
// applied through the REAL blend engine, must restore the coherent sum of a deliberately
// misaligned pair — this validates the sign conventions against felitronics::blend::shiftFrac
// rather than trusting them.
#include <felitronics_test.h>
#include "core/AutoAlign.h"
#include "core/BlendEngine.h"
#include "model/MixModel.h"

#include <cmath>
#include <vector>

using felitronics::test::ok;
using felitronics::test::group;

namespace
{
constexpr double kSr = 48000.0;

std::vector<float> testIr (size_t n, int onset)                        // a decaying "cab-ish" impulse
{
    std::vector<float> x (n, 0.0f);
    for (size_t i = (size_t) onset; i < n; ++i) {
        const double t = (double) (i - (size_t) onset);
        x[i] = (float) (std::exp (-t / 300.0) * std::sin (0.13 * t) + 0.4 * std::exp (-t / 90.0) * std::sin (0.61 * t));
    }
    return x;
}
double energy (const std::vector<float>& x) { double e = 0; for (float v : x) e += (double) v * v; return e; }

std::vector<float> blendPair (const std::vector<float>& a, const std::vector<float>& b,
                              const ocap::autoalign::Alignment& fix)
{
    std::vector<ocap::StripParams> strips (2);
    strips[1].shiftMs  = fix.shiftMs;
    strips[1].phaseDeg = fix.invert ? 180.0 : 0.0;
    const std::vector<std::vector<float>> irs { a, b };
    return ocap::blendIrs (irs, strips, ocap::MasterParams {}, kSr, false);
}
}

int main()
{
    std::printf ("orbitcapture auto-align tests\n");

    const auto ref = testIr (16384, 1000);

    group ("delayed + inverted channel: Auto restores the coherent sum");
    {
        auto bad = testIr (16384, 1000 + 53);                          // ~1.1 ms late, flipped
        for (auto& v : bad) v = -v;
        const auto fix = ocap::autoalign::alignOne (ref, bad, kSr, 2.0);
        ok (fix.invert, "polarity flip detected");
        ok (std::abs (fix.shiftMs - (-53.0 / kSr * 1000.0)) < 0.05, "lag found (~-1.10 ms to advance)");
        ok (fix.corr > 0.95, "high confidence on a clean pair");

        const double eRef = energy (ref);
        const double before = energy (blendPair (ref, bad, {}));       // no fix: combing
        const double after  = energy (blendPair (ref, bad, fix));      // fixed: ~coherent x4 energy
        ok (after > 3.6 * eRef, "fixed sum is nearly coherent (>= ~+5.6 dB over one mic)");
        ok (after > before * 1.5, "the fix beats the broken sum decisively");
    }

    group ("already-aligned channel: Auto is a no-op");
    {
        const auto fix = ocap::autoalign::alignOne (ref, ref, kSr, 2.0);
        ok (!fix.invert && std::abs (fix.shiftMs) < 0.011, "no shift, no flip on identical channels");
        ok (fix.corr > 0.99, "self-correlation is ~1");
    }

    group ("degenerate inputs stay identity");
    {
        const auto fix = ocap::autoalign::alignOne (ref, std::vector<float> (16384, 0.0f), kSr, 2.0);
        ok (fix.shiftMs == 0.0 && !fix.invert && fix.corr == 0.0, "silence: nothing to align");
        const auto whole = ocap::autoalign::align ({ ref }, kSr, 0, 2.0);
        ok (whole.size() == 1 && whole[0].shiftMs == 0.0, "single-channel console: identity");
    }

    return felitronics::test::report();
}
