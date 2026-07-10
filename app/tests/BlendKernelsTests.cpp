// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// Headless property tests for the lifted pure blend kernels (de-monolith step 2). JUCE-free — this is
// the payoff of pulling the DSP out of the god-object: it now unit-tests without a widget tree.
#include <felitronics_test.h>
#include "core/BlendKernels.h"
#include "core/AuditionRenderer.h"
#include "core/IrDeliverable.h"

#include <cmath>
#include <vector>

using felitronics::test::ok;
using felitronics::test::approx;
using felitronics::test::group;

namespace
{
double rms (const std::vector<float>& x, std::size_t from, std::size_t to)
{
    double s = 0.0; std::size_t c = 0;
    for (std::size_t i = from; i < to && i < x.size(); ++i) { s += (double) x[i] * x[i]; ++c; }
    return c ? std::sqrt (s / (double) c) : 0.0;
}
std::vector<float> sine (double f, double sr, std::size_t n)
{
    std::vector<float> x (n);
    for (std::size_t i = 0; i < n; ++i) x[i] = (float) std::sin (2.0 * felitronics::core::kPi * f * (double) i / sr);
    return x;
}
}

int main()
{
    std::printf ("orbitcapture blend-kernel tests\n");
    const double sr = 48000.0;

    // convolveDI with a unit impulse is identity.
    group ("convolveDI: δ is identity");
    {
        const std::vector<float> di { 1.0f, 2.0f, 3.0f, -4.0f };
        const auto y = ocap::convolveDI (di, { 1.0f });
        bool same = y.size() >= di.size();
        for (std::size_t i = 0; same && i < di.size(); ++i) same = std::fabs (y[i] - di[i]) < 1e-4f;
        ok (same, "di ⊛ δ == di");
    }

    // biquad HPF blocks DC; LPF passes it.
    group ("biquad DC response");
    {
        std::vector<float> hp (400, 1.0f); ocap::biquadInplace (hp, sr, 1000.0, true);
        ok (std::fabs (hp[399]) < 1e-3f, "HPF settles DC → 0");
        std::vector<float> lp (400, 1.0f); ocap::biquadInplace (lp, sr, 1000.0, false);
        ok (std::fabs (lp[399] - 1.0f) < 1e-2f, "LPF passes DC → ~1");
    }

    // applyBlendSlope is TRUE Butterworth: −3 dB (×0.708) at fc for EVERY slope (the 836177d fix).
    group ("applyBlendSlope −3 dB at fc (all slopes)");
    {
        const double fc = 1000.0;
        for (int slope : { 12, 24, 48, 96 })
        {
            auto v = sine (fc, sr, 8192);
            const double in = rms (v, 4096, 8192);            // steady-state input RMS
            ocap::applyBlendSlope (v, sr, fc, /*hp*/ true, slope);
            const double out = rms (v, 4096, 8192);
            approx (out / in, 0.7079, 0.03, "HPF@fc ratio ≈ −3 dB, slope " + std::to_string (slope));
        }
    }

    // rotatePhase: θ=180° is an exact polarity flip (sin180=0 kills the Hilbert term); θ=0 is a no-op.
    group ("rotatePhase 180°/0°");
    {
        auto x = sine (500.0, sr, 512);
        const auto orig = x;
        ocap::rotatePhase (x, 180.0);
        bool flip = true; for (std::size_t i = 0; i < x.size(); ++i) if (std::fabs (x[i] + orig[i]) > 1e-4f) { flip = false; break; }
        ok (flip, "θ=180° == polarity flip (y == −x)");
        auto y = orig; ocap::rotatePhase (y, 0.0);
        bool noop = true; for (std::size_t i = 0; i < y.size(); ++i) if (std::fabs (y[i] - orig[i]) > 1e-6f) { noop = false; break; }
        ok (noop, "θ=0 == no-op");
    }

    // shiftFrac by an integer moves a delta by exactly that many samples.
    group ("shiftFrac integer delay");
    {
        std::vector<float> d (64, 0.0f); d[10] = 1.0f;
        const auto y = ocap::shiftFrac (d, 5.0);
        ok (std::fabs (y[15] - 1.0f) < 1e-4f && std::fabs (y[10]) < 1e-4f, "δ@10 shifted +5 → δ@15");
    }

    // resampleIR preserves DC (kernel-sum normalised).
    group ("resampleIR DC preservation");
    {
        std::vector<float> c (200, 0.5f);
        const auto y = ocap::resampleIR (c, 48000.0, 96000.0);
        ok (y.size() > 200 && std::fabs (y[y.size() / 2] - 0.5) < 1e-3, "constant 0.5 upsampled stays 0.5");
    }

    return felitronics::test::report();
}
