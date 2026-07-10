// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// Oracle tests for post-processing (onset-trim, normalize) and the input gate.
#include "oc/fft.hpp"
#include "oc/sweep.hpp"
#include "oc/deconv.hpp"
#include "oc/post.hpp"
#include "oc/gate.hpp"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

using namespace oc;

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_fail; } \
    else         { std::printf("  ok  : %s\n", msg); } } while (0)

static std::vector<double> sweep_proper(const Sweep& sw) {
    return std::vector<double>(sw.x.begin(), sw.x.begin() + sw.sweep_len);
}

int main() {
    SweepSpec s;
    const Sweep sw = make_sweep(s);
    const auto xp = sweep_proper(sw);

    // ---- Onset detection / trim on a known onset
    std::printf("[1] onset-trim on known onset\n");
    {
        const std::size_t ON = 200, PRE = 8;
        std::vector<double> ir(1000, 0.0);
        for (std::size_t k = ON; k < ir.size(); ++k) {
            const double t = (double)(k - ON);
            ir[k] = std::exp(-t / 50.0) * std::sin(2 * kPi * 1500.0 * t / s.sr);
        }
        const auto o = detect_onset(ir, -40.0, PRE);
        std::printf("    onset=%zu (expect ~%zu)  peak_idx=%zu\n", o.onset, ON - PRE, o.peak_idx);
        CHECK(o.onset <= ON && o.onset + PRE + 4 >= ON, "onset within pre-roll of true attack");
        CHECK(o.peak_idx >= ON && o.peak_idx < ON + 80, "peak located inside the IR body");
        const auto t = trim_to_onset(ir, o, 256);
        double e = 0.0; for (double v : t) e += v * v;
        CHECK(e > 0.0, "trimmed IR retains the attack");
    }

    // ---- Peak normalize (non-destructive gain)
    std::printf("[2] peak normalize\n");
    {
        std::vector<double> x{0.1, -0.2, 0.05};
        apply_gain(x, peak_gain(x, 0.98));
        double pk = 0.0; for (double v : x) pk = std::max(pk, std::fabs(v));
        std::printf("    peak after = %.6f\n", pk);
        CHECK(std::fabs(pk - 0.98) < 1e-9, "normalized to target peak");
    }

    // ---- Gate: clean sweep response passes
    std::printf("[3] gate: clean sweep response passes\n");
    {
        std::vector<double> h(120, 0.0); h[10] = 1.0; h[30] = -0.5; h[70] = 0.25;
        const auto y = convolve(xp, h);
        const auto g = gate_recording(y, sw);
        std::printf("    peak=%.2f dBFS  snr=%.1f  clipped=%d present=%d ok=%d\n",
                    g.peak_dbfs, g.snr, g.clipped, g.sweep_present, g.ok);
        CHECK(!g.clipped, "not clipped");
        CHECK(g.sweep_present, "sweep detected");
        CHECK(g.ok, "gate passes a clean take");
    }

    // ---- Gate: clipped take rejected (nonlinearity would bleed into the IR)
    std::printf("[4] gate: clipped take rejected\n");
    {
        std::vector<double> h(120, 0.0); h[10] = 1.0;
        auto y = convolve(xp, h);
        for (double& v : y) { v *= 4.0; if (v > 1.0) v = 1.0; if (v < -1.0) v = -1.0; }
        const auto g = gate_recording(y, sw);
        std::printf("    peak=%.2f dBFS  clipped=%d ok=%d reason='%s'\n",
                    g.peak_dbfs, g.clipped, g.ok, g.reason.c_str());
        CHECK(g.clipped, "clip detected");
        CHECK(!g.ok, "gate rejects clipped take");
    }

    // ---- Gate: noise (no sweep) rejected
    std::printf("[5] gate: noise (no sweep) rejected\n");
    {
        std::vector<double> n(xp.size());
        std::uint64_t st = 88172645463325252ull;  // deterministic xorshift
        for (double& v : n) {
            st ^= st << 13; st ^= st >> 7; st ^= st << 17;
            v = ((double)(st >> 11) / 9007199254740992.0) * 2.0 - 1.0;
            v *= 0.3;
        }
        const auto g = gate_recording(n, sw);
        std::printf("    snr=%.2f present=%d ok=%d reason='%s'\n",
                    g.snr, g.sweep_present, g.ok, g.reason.c_str());
        CHECK(!g.sweep_present, "no sweep detected in noise");
        CHECK(!g.ok, "gate rejects noise");
    }

    // ---- Gate: hot-but-CLEAN take must NOT be flagged as clipped; flat-top must
    std::printf("[6] gate: hot clean vs flat-top clip\n");
    {
        std::vector<double> h(80, 0.0); h[10] = 1.0;
        auto y = convolve(xp, h);
        double pk = 0.0; for (double v : y) pk = std::max(pk, std::fabs(v));
        for (double& v : y) v *= 0.99 / pk;             // clean peak at 0.99 (−0.087 dBFS)
        const auto gc = gate_recording(y, sw);
        std::printf("    clean 0.99: clip_run=%d clipped=%d present=%d\n",
                    gc.clip_run, gc.clipped, gc.sweep_present);
        CHECK(!gc.clipped, "hot-but-clean -0.1 dBFS NOT flagged as clipped");
        for (std::size_t i = 1000; i < 1012; ++i) y[i] = 1.0;   // inject flat-top run
        const auto gk = gate_recording(y, sw);
        std::printf("    flat-top: clip_run=%d clipped=%d\n", gk.clip_run, gk.clipped);
        CHECK(gk.clipped, "flat-top run detected as clip");
    }

    // ---- Gate: reverberant (long-tail) take must NOT be false-rejected
    std::printf("[7] gate: reverberant take passes (SNR metric length-robust)\n");
    {
        const std::size_t Lh = 8000;
        std::vector<double> h(Lh, 0.0);
        for (std::size_t k = 0; k < Lh; ++k)
            h[k] = 0.1 * std::exp(-(double)k / 2000.0) * std::sin(2 * kPi * 800.0 * (double)k / s.sr);
        const auto y = convolve(xp, h);
        const auto g = gate_recording(y, sw);
        std::printf("    long tail: snr=%.1f present=%d\n", g.snr, g.sweep_present);
        CHECK(g.sweep_present, "reverberant sweep still detected (not length-penalized)");
    }

    // ---- make_sweep parameter validation (no NaN / no size_t underflow / no aliasing)
    std::printf("[8] sweep param validation (clamps)\n");
    {
        SweepSpec bad; bad.f1 = 0.0;
        const auto a = make_sweep(bad);
        bool fin = true; for (double v : a.x) if (!std::isfinite(v)) { fin = false; break; }
        CHECK(a.sweep_len >= 1 && fin, "f1=0 -> clamped, finite, no NaN");
        SweepSpec z; z.dur = 0.0;
        const auto b = make_sweep(z);
        CHECK(b.sweep_len >= 1, "dur=0 -> sweep_len>=1 (no size_t underflow)");
        SweepSpec hi; hi.f2 = 30000.0; hi.sr = 48000.0;
        const auto c = make_sweep(hi);
        std::printf("    f2 clamped to %.0f Hz (Nyquist 24000)\n", c.spec.f2);
        CHECK(c.spec.f2 <= 24000.0, "f2>Nyquist -> clamped below Nyquist");
    }

    std::printf(g_fail ? "\n=== %d CHECK(S) FAILED ===\n" : "\n=== ALL CHECKS PASSED ===\n", g_fail);
    return g_fail ? 1 : 0;
}
