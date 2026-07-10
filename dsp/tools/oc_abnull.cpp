// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// oc_abnull — objective A/B for IR faithfulness.
//   predicted = DI ⊛ IR   (what our IR says the mic should hear)
// align + level-match to the REAL miked recording, then report:
//   - null depth (dB the residual sits below the signal; deeper = truer IR)
//   - in-band spectral delta (dB)
// Tomorrow's Torpedo test made objective: feed the Captor DI, our IR, and the
// real mic recording; a deep null means the IR reproduces the cab faithfully.
#include "oc/wav.hpp"
#include "oc/fft.hpp"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

using namespace oc;

static double rms(const std::vector<double>& x, std::size_t a, std::size_t b) {
    double e = 0; std::size_t n = 0;
    for (std::size_t i = a; i < b && i < x.size(); ++i) { e += x[i] * x[i]; ++n; }
    return n ? std::sqrt(e / (double)n) : 0.0;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: %s <DI.wav> <IR.wav> <real_mic.wav>\n"
                    "  reports how well (DI * IR) matches the real miked recording.\n", argv[0]);
        return 2;
    }
    auto di = wav_read(argv[1]); auto ir = wav_read(argv[2]); auto mic = wav_read(argv[3]);
    for (auto* w : {&di, &ir, &mic}) if (!w->ok) { std::printf("read error: %s\n", w->error.c_str()); return 1; }
    if ((int)di.sr != (int)mic.sr || (int)di.sr != (int)ir.sr)
        std::printf("WARN: sample rates differ (DI %.0f, IR %.0f, mic %.0f) — results meaningless unless matched\n",
                    di.sr, ir.sr, mic.sr);

    const auto& d = di.ch[0]; const auto& h = ir.ch[0]; const auto& m = mic.ch[0];
    std::vector<double> pred = convolve(d, h);           // DI ⊛ IR
    std::printf("DI %zu, IR %zu, mic %zu, predicted %zu samples @ %.0f Hz\n",
                d.size(), h.size(), m.size(), pred.size(), mic.sr);

    // align pred to mic by cross-correlation (peak of mic ⊛ reverse(pred))
    std::vector<double> pr(pred.rbegin(), pred.rend());
    auto cc = convolve(m, pr);
    std::size_t pk = 0; double best = -1;
    for (std::size_t i = 0; i < cc.size(); ++i) { double a = std::fabs(cc[i]); if (a > best) { best = a; pk = i; } }
    const long lag = (long)pk - (long)(pred.size() - 1);  // pred[n] aligns to mic[n+lag]
    std::printf("alignment lag = %ld samples (%.2f ms)\n", lag, 1000.0 * lag / mic.sr);

    // overlap region of mic and shifted pred; least-squares gain; residual null
    const long lo = std::max(0L, lag);
    const long hi = std::min((long)m.size(), (long)pred.size() + lag);
    if (hi - lo < 64) { std::printf("no usable overlap\n"); return 1; }
    double num = 0, den = 0;
    for (long i = lo; i < hi; ++i) { double p = pred[(size_t)(i - lag)]; num += m[(size_t)i] * p; den += p * p; }
    const double g = den > 0 ? num / den : 0.0;
    double eres = 0, esig = 0;
    for (long i = lo; i < hi; ++i) { double r = m[(size_t)i] - g * pred[(size_t)(i - lag)]; eres += r * r; esig += m[(size_t)i] * m[(size_t)i]; }
    const double nullDb = (esig > 0) ? 20.0 * std::log10(std::sqrt(eres / esig)) : 0.0;
    std::printf("match gain = %.3f (%.1f dB)\n", g, 20.0 * std::log10(std::fabs(g) + 1e-12));
    std::printf("NULL DEPTH = %.1f dB below signal   (deeper = truer IR; > -20 good, > -30 excellent)\n", nullDb);

    // in-band spectral delta of aligned/scaled pred vs mic
    const std::size_t L = (std::size_t)(hi - lo);
    std::vector<double> ms(L), ps(L);
    for (std::size_t i = 0; i < L; ++i) { ms[i] = m[(size_t)(lo + (long)i)]; ps[i] = g * pred[(size_t)(lo + (long)i - lag)]; }
    std::size_t nfft = 1; while (nfft < L) nfft <<= 1; nfft = std::max<std::size_t>(nfft, 8192);
    auto Mm = mag_spectrum(ms, nfft), Mp = mag_spectrum(ps, nfft);
    const double binHz = mic.sr / (double)nfft;
    const std::size_t blo = (std::size_t)std::ceil(100.0 / binHz), bhi = (std::size_t)std::floor(6000.0 / binHz);
    double s = 0; std::size_t c = 0;
    for (std::size_t i = blo; i <= bhi && i < Mm.size(); ++i) { s += std::fabs(20.0 * std::log10((Mp[i] + 1e-9) / (Mm[i] + 1e-9))); ++c; }
    std::printf("in-band [100-6k] mean |ΔdB| = %.2f dB (predicted vs real)\n", c ? s / (double)c : 0.0);
    return 0;
}
