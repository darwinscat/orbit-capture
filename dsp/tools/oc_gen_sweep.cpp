// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// oc_gen_sweep — write OUR ESS sweep to a WAV so it can be played through the rig
// (DAW / REW generator) and the mic return recorded. The recording is then fed to
// oc_deconv, which deconvolves against THIS exact sweep (matched by construction).
#include "oc/sweep.hpp"
#include "oc/wav.hpp"
#include <cstdio>
#include <cstdlib>

using namespace oc;

int main(int argc, char** argv) {
    const char* out = (argc > 1) ? argv[1] : "sweep.wav";
    const double sr  = (argc > 2) ? std::atof(argv[2]) : 48000.0;
    SweepSpec s; s.sr = sr;
    const Sweep sw = make_sweep(s);
    if (!wav_write(out, {sw.x}, sw.spec.sr, 24, false)) { std::printf("write failed: %s\n", out); return 1; }
    std::printf("wrote %s\n  ESS %.0f-%.0f Hz, %.1f s sweep (+%.1f s tail), %.0f Hz, 24-bit, %zu samples\n"
                "  play this through amp->cab->mic, record the mic, feed the recording to oc_deconv.\n",
                out, sw.spec.f1, sw.spec.f2, sw.spec.dur, sw.spec.tail, sw.spec.sr, sw.x.size());
    return 0;
}
