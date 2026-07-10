// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// Oracle tests for the OrbitCapture sweep/deconv core.
// The point: correctness is objective here — the code either recovers the known
// ground truth to numerical precision, or it doesn't. No golden files.
#include "oc/fft.hpp"
#include "oc/sweep.hpp"
#include "oc/deconv.hpp"
#include <cstdio>
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
static std::size_t argmax_abs(const std::vector<double>& v, std::size_t lo, std::size_t hi) {
    std::size_t bi = lo; double bv = -1.0;
    for (std::size_t i = lo; i < hi && i < v.size(); ++i) {
        const double a = std::fabs(v[i]);
        if (a > bv) { bv = a; bi = i; }
    }
    return bi;
}

int main() {
    SweepSpec s; // defaults: 20 Hz→20 kHz, 5 s @ 48 k, -6 dBFS, 50 ms fade, 1 s tail
    std::printf("Building sweep %.0f→%.0f Hz, %.1fs @ %.0f Hz ...\n", s.f1, s.f2, s.dur, s.sr);
    const Sweep sw = make_sweep(s);
    const auto xp = sweep_proper(sw);
    std::printf("  sweep_len=%zu  L=%.5f s  inv_len=%zu\n\n", sw.sweep_len, sw.L, sw.inv.size());

    // ---- Oracle 1: sweep ⊛ inverse ≈ δ (unit peak at lag N-1, bounded sidelobes)
    std::printf("[1] sweep ⊛ inverse ≈ δ\n");
    {
        const auto g = convolve(xp, sw.inv);
        const std::size_t pk = argmax_abs(g, 0, g.size());
        std::printf("    peak idx=%zu (expect %zu)  peak=%.6f\n", pk, sw.sweep_len - 1, g[pk]);
        CHECK(pk == sw.sweep_len - 1, "delta peak at lag N-1");
        CHECK(std::fabs(g[pk] - 1.0) < 1e-3, "delta peak ~ 1.0");
        double eout = 0.0, epk = g[pk] * g[pk];
        for (std::size_t i = 0; i < g.size(); ++i)
            if (i + 64 < pk || i > pk + 64) eout += g[i] * g[i];
        std::printf("    out-of-window energy / peak^2 = %.5g\n", eout / epk);
        CHECK(eout / epk < 0.5, "band-limited delta: sidelobes bounded");
    }

    // ---- Oracle 2: known-answer, single delayed unit impulse
    std::printf("[2] known-answer: delayed unit impulse (d=137)\n");
    {
        const std::size_t d = 137;
        std::vector<double> h(d + 1, 0.0); h[d] = 1.0;
        const auto y  = convolve(xp, h);
        const auto dr = deconvolve(y, sw);
        const std::size_t expect = sw.sweep_len - 1 + d;
        const std::size_t pk = argmax_abs(dr.full, 0, dr.full.size());
        std::printf("    peak idx=%zu (expect %zu)  amp=%.6f\n", pk, expect, dr.full[pk]);
        CHECK(pk == expect, "impulse recovered at lag N-1+d");
        CHECK(std::fabs(dr.full[pk] - 1.0) < 1e-3, "amplitude ~ 1.0");
        CHECK(dr.full[pk] > 0.0, "polarity preserved");
    }

    // ---- Oracle 3: known-answer, multi-tap band-limited IR — in-band mag fidelity
    std::printf("[3] known-answer: damped 1 kHz sine IR, in-band magnitude\n");
    {
        const std::size_t Lh = 256;
        std::vector<double> h(Lh, 0.0);
        for (std::size_t k = 0; k < Lh; ++k)
            h[k] = std::exp(-(double)k / 40.0) * std::sin(2 * kPi * 1000.0 * (double)k / s.sr);
        const auto y   = convolve(xp, h);
        const auto dr  = deconvolve(y, sw);
        const auto rec = extract_ir(dr, 512);

        const std::size_t nfft = 4096;
        const auto Mh = mag_spectrum(h,   nfft);
        const auto Mr = mag_spectrum(rec, nfft);
        const double binHz = s.sr / (double)nfft;
        const std::size_t lo = (std::size_t)std::ceil(200.0 / binHz);
        const std::size_t hi = (std::size_t)std::floor(8000.0 / binHz);
        // Gain-agnostic fidelity: a constant in-band dB offset is just the deconv's
        // gain convention (IRs get normalized anyway). The real correctness signal is
        // how FLAT ΔdB is around that offset — i.e. is the spectral SHAPE recovered.
        std::vector<double> dbv; dbv.reserve(hi - lo + 1);
        double sum = 0.0;
        for (std::size_t i = lo; i <= hi; ++i) {
            const double a = Mh[i] + 1e-12, b = Mr[i] + 1e-12;
            const double dB = 20.0 * std::log10(b / a);
            dbv.push_back(dB); sum += dB;
        }
        const double gain = sum / (double)dbv.size();       // deconv in-band gain (dB)
        double var = 0.0;
        for (double v : dbv) var += (v - gain) * (v - gain);
        const double rms = std::sqrt(var / (double)dbv.size());
        std::printf("    in-band [200,8000] Hz  gain=%.3f dB (convention)  shape RMS=%.4f dB\n",
                    gain, rms);
        CHECK(rms < 0.5, "spectral SHAPE recovered (gain-removed RMS < 0.5 dB)");
    }

    // ---- Oracle 4: harmonic separation (the reason to use ESS)
    std::printf("[4] harmonic separation: images land at L*ln(k) advance\n");
    {
        // memoryless nonlinearity through a delta system → harmonics only
        std::vector<double> y(xp.size());
        for (std::size_t i = 0; i < xp.size(); ++i) {
            const double u = xp[i];
            y[i] = u + 0.25 * u * u + 0.12 * u * u * u;
        }
        const auto dr = deconvolve(y, sw);
        const long lin = (long)sw.sweep_len - 1;
        for (int k = 2; k <= 3; ++k) {
            const double adv = harmonic_advance_samples(sw, k);
            const long center = lin - (long)std::llround(adv);
            long lo = center - 60, hi = center + 60; if (lo < 0) lo = 0;
            const std::size_t pk = argmax_abs(dr.full, (std::size_t)lo, (std::size_t)hi);
            const long err = (long)pk - center;
            std::printf("    harmonic %d: predicted lag %ld, found %zu (err %ld samp)\n",
                        k, center, pk, err);
            CHECK(std::labs(err) <= 8, "harmonic peak near predicted advance");
        }
    }

    // ---- Oracle 5: round-trip latency absorbed (extract must not assume τ=0)
    std::printf("[5] round-trip latency absorbed (τ=2000)\n");
    {
        const std::size_t TAU = 2000;
        std::vector<double> y(TAU, 0.0);
        y.insert(y.end(), xp.begin(), xp.end());        // sweep delayed by τ; IR = δ at 0
        const auto dr = deconvolve(y, sw);
        std::printf("    measured latency=%zu (expect ~%zu)  linear_lag=%zu\n",
                    dr.latency, TAU, dr.linear_lag);
        CHECK(dr.latency + 80 >= TAU && dr.latency <= TAU + 20, "latency measured ~ τ");
        const auto rec = extract_ir(dr, 256);
        const std::size_t pk = argmax_abs(rec, 0, rec.size());
        std::printf("    recovered peak in window: idx=%zu amp=%.4f (fixed-lag code → ~0)\n",
                    pk, rec[pk]);
        CHECK(std::fabs(rec[pk]) > 0.5, "IR recovered despite latency");
    }

    std::printf(g_fail ? "\n=== %d CHECK(S) FAILED ===\n" : "\n=== ALL CHECKS PASSED ===\n", g_fail);
    return g_fail ? 1 : 0;
}
