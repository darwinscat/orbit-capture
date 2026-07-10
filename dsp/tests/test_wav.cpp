// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// Round-trip tests for the WAV codec: write → read → compare within quantization.
#include "oc/wav.hpp"
#include "oc/fft.hpp"   // kPi
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

using namespace oc;

static int g_fail = 0;
#define CHECK(c, m) do { \
    if (!(c)) { std::printf("  FAIL: %s\n", m); ++g_fail; } \
    else      { std::printf("  ok  : %s\n", m); } } while (0)

static double maxdiff(const std::vector<double>& a, const std::vector<double>& b) {
    double d = 0.0; const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) d = std::max(d, std::fabs(a[i] - b[i]));
    return d;
}

int main() {
    const double SR = 48000.0; const std::size_t N = 1000;
    std::vector<double> x(N);
    for (std::size_t i = 0; i < N; ++i)
        x[i] = 0.7 * std::sin(2 * kPi * 440.0 * (double)i / SR)
             + 0.2 * std::sin(2 * kPi * 3000.0 * (double)i / SR);
    const std::string p = "oc_wav_rt.wav";

    std::printf("[1] float32 mono round-trip\n");
    CHECK(wav_write(p, {x}, SR, 32, true), "write f32");
    {
        const auto r = wav_read(p);
        CHECK(r.ok, "read ok");
        CHECK(r.ch.size() == 1 && r.frames() == N, "dims preserved");
        CHECK((int)r.sr == (int)SR && r.bits == 32 && r.is_float, "sr/bits/float preserved");
        const double d = maxdiff(x, r.ch[0]);
        std::printf("    maxdiff=%.3g\n", d);
        CHECK(d < 1e-6, "f32 lossless");
    }

    std::printf("[2] pcm24 mono round-trip\n");
    CHECK(wav_write(p, {x}, SR, 24, false), "write pcm24");
    {
        const auto r = wav_read(p);
        const double d = maxdiff(x, r.ch[0]);
        std::printf("    maxdiff=%.3g bits=%d\n", d, r.bits);
        CHECK(r.bits == 24 && !r.is_float, "bits=24 pcm");
        CHECK(d < 2e-6, "24-bit within LSB");
    }

    std::printf("[3] pcm16 mono round-trip\n");
    CHECK(wav_write(p, {x}, SR, 16, false), "write pcm16");
    {
        const auto r = wav_read(p);
        const double d = maxdiff(x, r.ch[0]);
        std::printf("    maxdiff=%.3g\n", d);
        CHECK(d < 5e-5, "16-bit within LSB");
    }

    std::printf("[4] float32 stereo round-trip\n");
    std::vector<double> y(N);
    for (std::size_t i = 0; i < N; ++i) y[i] = -0.5 * x[i];
    CHECK(wav_write(p, {x, y}, SR, 32, true), "write stereo");
    {
        const auto r = wav_read(p);
        CHECK(r.ch.size() == 2, "2 channels");
        const bool two = r.ch.size() >= 2;
        CHECK(two && maxdiff(x, r.ch[0]) < 1e-6 && maxdiff(y, r.ch[1]) < 1e-6, "both channels lossless");
    }

    // ---- Write-path validation (adversarial-review fixes)
    std::printf("[5] write validation: unsupported format, NaN/Inf, ragged channels\n");
    {
        CHECK(!wav_write(p, {x}, SR, 8, false),  "reject 8-bit write");
        CHECK(!wav_write(p, {x}, SR, 32, false), "reject 32-bit int write");
        std::vector<double> nz = x; nz[3] = std::nan(""); nz[7] = INFINITY;
        CHECK(wav_write(p, {nz}, SR, 24, false), "write with NaN/Inf succeeds");
        const auto r1 = wav_read(p);
        bool fin = r1.ok && !r1.ch.empty();
        if (fin) for (double v : r1.ch[0]) if (!std::isfinite(v)) { fin = false; break; }
        CHECK(fin, "NaN/Inf sanitized to finite on write");
        std::vector<double> shortc(10, 0.1);
        CHECK(wav_write(p, {x, shortc}, SR, 32, true), "ragged channels write (min length, no OOB)");
        const auto r2 = wav_read(p);
        CHECK(r2.ok && r2.frames() == 10, "ragged -> min length (10 frames)");
    }

    std::remove(p.c_str());
    std::printf(g_fail ? "\n=== %d CHECK(S) FAILED ===\n" : "\n=== ALL CHECKS PASSED ===\n", g_fail);
    return g_fail ? 1 : 0;
}
