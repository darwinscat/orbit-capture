// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// OrbitCapture DSP — minimal, self-contained WAV read/write (no libsndfile).
// Reads canonical PCM/float WAVE (16/24/32-bit int, 32/64-bit float, incl.
// WAVE_FORMAT_EXTENSIBLE), deinterleaves to per-channel doubles in [-1,1].
// Writes 16/24-bit PCM or 32-bit float. Little-endian (the WAV convention).
#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace oc {

struct WavData {
    std::vector<std::vector<double>> ch;  // ch[c][n], normalized to [-1,1]
    double      sr       = 0.0;
    int         bits     = 0;
    bool        is_float = false;
    bool        ok       = false;
    std::string error;
    std::size_t frames() const { return ch.empty() ? 0 : ch[0].size(); }
};

inline std::uint32_t oc_rd_u32(const std::uint8_t* p) {
    return (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) |
           ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24);
}
inline std::uint16_t oc_rd_u16(const std::uint8_t* p) {
    return (std::uint16_t)(p[0] | (p[1] << 8));
}

inline WavData wav_read(const std::string& path) {
    WavData w;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { w.error = "cannot open " + path; return w; }
    std::fseek(f, 0, SEEK_END); long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    if (sz < 44) { std::fclose(f); w.error = "too small"; return w; }
    if (sz > (512L << 20)) { std::fclose(f); w.error = "file too large (>512 MB)"; return w; }  // DoS guard
    std::vector<std::uint8_t> b((std::size_t)sz);
    if (std::fread(b.data(), 1, (std::size_t)sz, f) != (std::size_t)sz) {
        std::fclose(f); w.error = "read failed"; return w;
    }
    std::fclose(f);
    if (std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0) {
        w.error = "not a WAVE file"; return w;
    }
    std::size_t pos = 12;
    std::uint16_t fmt = 0, nch = 0, bits = 0; std::uint32_t rate = 0;
    const std::uint8_t* data = nullptr; std::size_t datalen = 0;
    while (pos + 8 <= b.size()) {
        const std::uint8_t* id = b.data() + pos;
        const std::uint32_t clen = oc_rd_u32(b.data() + pos + 4);
        const std::size_t body = pos + 8;
        if (std::memcmp(id, "fmt ", 4) == 0 && clen >= 16 && body + 16 <= b.size()) {
            fmt  = oc_rd_u16(b.data() + body);
            nch  = oc_rd_u16(b.data() + body + 2);
            rate = oc_rd_u32(b.data() + body + 4);
            bits = oc_rd_u16(b.data() + body + 14);
            if (fmt == 0xFFFE && body + 26 <= b.size()) fmt = oc_rd_u16(b.data() + body + 24);
        } else if (std::memcmp(id, "data", 4) == 0) {
            data = b.data() + body;
            datalen = std::min<std::size_t>(clen, b.size() - body);
        }
        pos = body + clen + (clen & 1);   // chunks are word-aligned
    }
    if (!data || nch == 0 || bits == 0) { w.error = "missing fmt/data"; return w; }
    // Reject unsupported formats loudly — do NOT return silent zeros with ok=true.
    const bool supported = (fmt == 3 && (bits == 32 || bits == 64)) ||
                           (fmt == 1 && (bits == 16 || bits == 24 || bits == 32));
    if (!supported) {
        w.error = "unsupported WAV format (fmt=" + std::to_string(fmt) +
                  ", bits=" + std::to_string(bits) + ")";
        return w;
    }
    w.sr = rate; w.bits = bits; w.is_float = (fmt == 3);
    const std::size_t bytes = bits / 8;
    const std::size_t frame = bytes * nch;
    const std::size_t nf = frame ? datalen / frame : 0;
    w.ch.assign(nch, std::vector<double>(nf, 0.0));
    for (std::size_t i = 0; i < nf; ++i)
        for (std::uint16_t c = 0; c < nch; ++c) {
            const std::uint8_t* s = data + i * frame + c * bytes;
            double v = 0.0;
            if (fmt == 3 && bits == 32)      { float  fv; std::memcpy(&fv, s, 4); v = fv; }
            else if (fmt == 3 && bits == 64) { double dv; std::memcpy(&dv, s, 8); v = dv; }
            else if (bits == 16) { std::int16_t iv = (std::int16_t)oc_rd_u16(s); v = iv / 32768.0; }
            else if (bits == 24) {
                std::int32_t iv = (std::int32_t)(s[0] | (s[1] << 8) | (s[2] << 16));
                if (iv & 0x800000) iv -= 0x1000000;   // sign-extend (no impl-defined cast)
                v = iv / 8388608.0;
            }
            else if (bits == 32) { std::int32_t iv = (std::int32_t)oc_rd_u32(s); v = iv / 2147483648.0; }
            w.ch[c][i] = v;
        }
    w.ok = true; return w;
}

inline void oc_wr_u32(std::vector<std::uint8_t>& o, std::uint32_t v) {
    o.push_back(v & 0xFF); o.push_back((v >> 8) & 0xFF);
    o.push_back((v >> 16) & 0xFF); o.push_back((v >> 24) & 0xFF);
}
inline void oc_wr_u16(std::vector<std::uint8_t>& o, std::uint16_t v) {
    o.push_back(v & 0xFF); o.push_back((v >> 8) & 0xFF);
}
inline void oc_wr_tag(std::vector<std::uint8_t>& o, const char* t) { o.insert(o.end(), t, t + 4); }

inline bool wav_write(const std::string& path, const std::vector<std::vector<double>>& ch,
                      double sr, int bits, bool is_float) {
    if (ch.empty()) return false;
    const bool supported = (is_float && bits == 32) || (!is_float && (bits == 16 || bits == 24));
    if (!supported) return false;
    std::size_t nf = ch[0].size();
    for (const auto& c : ch) nf = std::min(nf, c.size());   // guard ragged channels (no OOB)
    if (nf == 0) return false;
    const std::uint16_t nch = (std::uint16_t)ch.size();
    const std::uint16_t fmt = is_float ? 3 : 1;
    const std::uint32_t rate = (std::uint32_t)std::llround(sr);
    const std::uint16_t block = (std::uint16_t)(nch * (bits / 8));
    const std::uint32_t datalen = (std::uint32_t)(nf * block);
    std::vector<std::uint8_t> o; o.reserve(44 + datalen);
    oc_wr_tag(o, "RIFF"); oc_wr_u32(o, 36 + datalen); oc_wr_tag(o, "WAVE");
    oc_wr_tag(o, "fmt "); oc_wr_u32(o, 16); oc_wr_u16(o, fmt); oc_wr_u16(o, nch);
    oc_wr_u32(o, rate); oc_wr_u32(o, rate * block); oc_wr_u16(o, block); oc_wr_u16(o, (std::uint16_t)bits);
    oc_wr_tag(o, "data"); oc_wr_u32(o, datalen);
    auto clampd = [](double v) {
        if (!std::isfinite(v)) return 0.0;                  // NaN/Inf → 0 (no llround UB)
        return v < -1.0 ? -1.0 : (v > 1.0 ? 1.0 : v);
    };
    for (std::size_t i = 0; i < nf; ++i)
        for (std::uint16_t c = 0; c < nch; ++c) {
            const double v = ch[c][i];
            if (is_float && bits == 32) { float fv = std::isfinite(v) ? (float)v : 0.0f; std::uint8_t t[4]; std::memcpy(t, &fv, 4); o.insert(o.end(), t, t + 4); }
            else if (bits == 16) { std::int16_t iv = (std::int16_t)std::llround(clampd(v) * 32767.0); oc_wr_u16(o, (std::uint16_t)iv); }
            else if (bits == 24) {
                std::int32_t iv = (std::int32_t)std::llround(clampd(v) * 8388607.0);
                o.push_back(iv & 0xFF); o.push_back((iv >> 8) & 0xFF); o.push_back((iv >> 16) & 0xFF);
            }
        }
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(o.data(), 1, o.size(), f) == o.size();
    std::fclose(f);
    return ok;
}

inline bool wav_write_mono_f32(const std::string& path, const std::vector<double>& x, double sr) {
    return wav_write(path, {x}, sr, 32, true);
}

} // namespace oc
