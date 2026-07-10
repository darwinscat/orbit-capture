// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// BYTE-NULL identity gate (de-monolith step 4). The kernels were MOVED verbatim into felitronics::blend;
// exported IR libraries depend on their EXACT numerics. This fuzzes the FROZEN pre-step-4 app kernels
// (ocap::legacy) against the promoted felitronics::blend kernels in THIS exact compiler/flags and
// requires a bit-for-bit (memcmp) match. Byte-identity is per-compiler, so this lives app-side (core CI
// cross-platform uses tolerance + a scipy NULL).
#include <felitronics_test.h>
#include "LegacyBlendKernels.h"                 // ocap::legacy — frozen
#include <felitronics/blend/BlendKernels.h>     // felitronics::blend — promoted

#include <cstring>
#include <cmath>
#include <vector>

using felitronics::test::ok;
using felitronics::test::group;

namespace
{
// Deterministic LCG noise in [-1,1] — reproducible, no Math::random.
std::vector<float> gen (std::size_t n, unsigned seed)
{
    std::vector<float> x (n);
    unsigned s = seed * 2654435761u + 1u;
    for (auto& v : x) { s = s * 1664525u + 1013904223u; v = (float) ((double) (s >> 8) / (double) (1u << 24) - 0.5) * 2.0f; }
    return x;
}
bool bitEqual (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
}
}

int main()
{
    std::printf ("orbitcapture blend byte-NULL (frozen legacy vs felitronics::blend)\n");
    const double sr = 48000.0;
    namespace fb = felitronics::blend;

    group ("biquadInplace bit-exact");
    {
        bool eq = true;
        for (unsigned seed = 0; seed < 24; ++seed)
        {
            const double fc = 40.0 + (double) ((seed * 331u) % 9000u);
            for (bool hp : { true, false })
            {
                auto a = gen (4096, seed), b = a;
                ocap::legacy::biquadInplace (a, sr, fc, hp);
                fb::biquadInplace (b, sr, fc, hp);
                if (! bitEqual (a, b)) eq = false;
            }
        }
        ok (eq, "RBJ biquad HP/LP identical over 24×2 fuzz cases");
    }

    group ("onePoleInplace bit-exact");
    {
        bool eq = true;
        for (unsigned seed = 0; seed < 24; ++seed)
            for (bool hp : { true, false })
            {
                const double fc = 40.0 + (double) ((seed * 271u) % 9000u);
                auto a = gen (4096, seed + 100u), b = a;
                ocap::legacy::onePoleInplace (a, sr, fc, hp);
                fb::onePoleInplace (b, sr, fc, hp);
                if (! bitEqual (a, b)) eq = false;
            }
        ok (eq, "one-pole HP/LP identical");
    }

    group ("applyBlendSlope bit-exact (all slopes)");
    {
        bool eq = true;
        for (int slope : { 6, 12, 24, 36, 48, 72, 96 })
            for (unsigned seed = 0; seed < 8; ++seed)
                for (bool hp : { true, false })
                {
                    const double fc = 80.0 + (double) ((seed * 411u) % 8000u);
                    auto a = gen (4096, seed + 200u), b = a;
                    ocap::legacy::applyBlendSlope (a, sr, fc, hp, slope);
                    fb::applyBlendSlope (b, sr, fc, hp, slope);
                    if (! bitEqual (a, b)) eq = false;
                }
        ok (eq, "Butterworth slope cascade identical over {6..96}");
    }

    group ("rotatePhase bit-exact");
    {
        bool eq = true;
        for (double deg : { -180.0, -90.0, -37.5, 0.25, 45.0, 90.0, 179.0 })
            for (unsigned seed = 0; seed < 8; ++seed)
            {
                auto a = gen (3000, seed + 300u), b = a;
                ocap::legacy::rotatePhase (a, deg);
                fb::rotatePhase (b, deg);
                if (! bitEqual (a, b)) eq = false;
            }
        ok (eq, "Hilbert phase rotation identical over angles×seeds");
    }

    group ("shiftFrac bit-exact");
    {
        bool eq = true;
        for (double d : { -12.0, -3.7, -0.005, 0.0001, 2.5, 17.0 })
            for (unsigned seed = 0; seed < 8; ++seed)
            {
                const auto x = gen (2048, seed + 400u);
                if (! bitEqual (ocap::legacy::shiftFrac (x, d), fb::shiftFrac (x, d))) eq = false;
            }
        ok (eq, "fractional shift identical over shifts×seeds");
    }

    return felitronics::test::report();
}
