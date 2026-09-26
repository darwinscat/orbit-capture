// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// OrbitCapture DSP — minimal, self-contained WAV read/write (no libsndfile).
//
// COMPAT SHIM: delegates to felitronics-core (`felitronics::io`, felitronics/io/Wav.h). The codec was
// moved up there verbatim (reuse audit E6) and core owns it now; this keeps the `oc::` spelling and the
// WavData field names so the app, the tools and the tests compile unchanged.
//
// Reads 16/24/32-bit PCM and 32/64-bit float (incl. WAVE_FORMAT_EXTENSIBLE) into per-channel doubles;
// writes 16/24-bit PCM or 32-bit float. ONE PCM grid, felitronics::dither's: a `bits`-bit code k stands
// for k / 2^(bits-1) both ways, so write∘read is the identity on every code. The local copy this replaces
// wrote PCM as llround(v · (2^(bits-1) − 1)) — one LSB narrower than its own reader: every |k| > 2^(bits-2)
// came back one LSB toward zero (32767 of the 65536 16-bit codes, 8388607 of the 24-bit ones), and a
// dithered signal was quantized a second time, undithered. 32-bit float writes are byte-identical to it.
// Core's reader is also stricter than the copy was: a chunk overrunning the file, data that is not
// frame-aligned or a malformed EXTENSIBLE header is refused (ok=false + error) instead of truncated or
// guessed; the writer refuses a sample rate / channel count / size the RIFF header cannot hold.
#pragma once
#include <felitronics/io/Wav.h>
#include <string>
#include <vector>

namespace oc {

using WavData = felitronics::io::WavData;   // ch[c][n] in [-1,1], sr, bits, is_float, ok, error, frames()

inline WavData wav_read(const std::string& path) { return felitronics::io::readWav(path); }

inline bool wav_write(const std::string& path, const std::vector<std::vector<double>>& ch,
                      double sr, int bits, bool is_float) {
    return felitronics::io::writeWav(path, ch, sr, bits, is_float);
}

inline bool wav_write_mono_f32(const std::string& path, const std::vector<double>& x, double sr) {
    return felitronics::io::writeWavMonoF32(path, x, sr);
}

} // namespace oc
