// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// oc_deconv — end-to-end CLI harness: a raw recording of OUR sweep → cabinet IR.
// Reads a WAV (left channel), runs the input gate, deconvolves against the
// default sweep (generated at the file's SR), latency-corrects, onset-trims,
// normalizes, and writes a 32-bit float mono IR. This is the harness for the
// REW cross-check: feed a real raw sweep recording, compare our IR to REW's.
#include "oc/wav.hpp"
#include "oc/sweep.hpp"
#include "oc/deconv.hpp"
#include "oc/post.hpp"
#include "oc/gate.hpp"
#include <cstdio>
#include <cstdlib>

using namespace oc;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: %s <raw_sweep_recording.wav> <out_ir.wav> [ir_len=2048]\n"
                    "  Deconvolves a recording of the default OrbitCapture sweep\n"
                    "  (20 Hz-20 kHz, 5 s, generated at the file's sample rate).\n", argv[0]);
        return 2;
    }
    std::size_t ir_len = 2048;
    if (argc > 3) {
        char* end = nullptr;
        const long v = std::strtol(argv[3], &end, 10);
        if (end == argv[3] || *end != '\0' || v <= 0 || v > (1 << 20)) {
            std::printf("bad ir_len '%s' (expected 1..%d)\n", argv[3], 1 << 20);
            return 2;
        }
        ir_len = (std::size_t)v;
    }

    WavData w = wav_read(argv[1]);
    if (!w.ok) { std::printf("read error: %s\n", w.error.c_str()); return 1; }
    std::printf("in : %.0f Hz, %d-bit%s, %zu ch, %zu frames\n",
                w.sr, w.bits, w.is_float ? " float" : "", w.ch.size(), w.frames());
    if (!(w.sr >= 8000.0 && w.sr <= 384000.0)) { std::printf("unsupported sample rate %.0f\n", w.sr); return 1; }
    if (w.frames() == 0) { std::printf("empty recording\n"); return 1; }
    const std::vector<double>& rec = w.ch[0];   // LEFT channel

    SweepSpec s; s.sr = w.sr;
    const Sweep sw = make_sweep(s);

    const GateReport g = gate_recording(rec, sw);
    std::printf("gate: peak=%.2f dBFS  clip_run=%d clipped=%d  snr=%.3g present=%d  ok=%d %s\n",
                g.peak_dbfs, g.clip_run, g.clipped, g.snr, g.sweep_present, g.ok,
                g.reason.empty() ? "" : ("[" + g.reason + "]").c_str());
    if (!g.ok) { std::printf("REJECTED: %s — not writing an IR (garbage-in guard).\n", g.reason.c_str()); return 3; }

    const DeconvResult dr = deconvolve(rec, sw);
    std::printf("deconv: latency=%zu samp (%.2f ms), linear_lag=%zu\n",
                dr.latency, 1000.0 * (double)dr.latency / w.sr, dr.linear_lag);

    std::vector<double> ir = extract_ir(dr, ir_len + 64);
    const OnsetResult on = detect_onset(ir);
    std::vector<double> out = trim_to_onset(ir, on, ir_len);
    apply_gain(out, peak_gain(out, 0.98));

    if (!wav_write_mono_f32(argv[2], out, w.sr)) { std::printf("write failed: %s\n", argv[2]); return 1; }
    std::printf("out: %s  (%zu samples, %.0f Hz, 32f)\n", argv[2], out.size(), w.sr);
    return 0;
}
