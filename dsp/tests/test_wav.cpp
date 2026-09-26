// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// Round-trip tests for the WAV codec: write → read → compare within quantization, and the PCM grid
// ([6]–[9]): a `bits`-bit code k stands for k / 2^(bits-1) BOTH ways (felitronics::dither's grid), so
// write∘read is the identity on every code. Codes are read from the file's BYTES as well as through
// wav_read — the reader's divisor is the other half of the claim and must not be the only witness.
#include "oc/wav.hpp"
#include "oc/fft.hpp"   // kPi
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
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

// ---- PCM-grid instruments ----------------------------------------------------------------------------
static double full(int bits) { return std::ldexp(1.0, bits - 1); }   // 2^(bits-1): 32768 / 8388608

static std::vector<std::uint8_t> file_bytes(const std::string& p) {
    std::vector<std::uint8_t> b;
    if (std::FILE* f = std::fopen(p.c_str(), "rb")) {
        std::uint8_t buf[65536]; std::size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
        std::fclose(f);
    }
    return b;
}
static bool put_bytes(const std::string& p, const std::vector<std::uint8_t>& b) {
    std::FILE* f = std::fopen(p.c_str(), "wb"); if (!f) return false;
    const bool okw = std::fwrite(b.data(), 1, b.size(), f) == b.size(); std::fclose(f); return okw;
}

// The codes a mono canonical (44-byte header) PCM image carries, straight from its bytes.
static std::vector<long long> codes_of(const std::vector<std::uint8_t>& img, int bits) {
    std::vector<long long> k; const std::size_t bp = (std::size_t)bits / 8;
    if (img.size() < 44 || std::memcmp(img.data() + 36, "data", 4) != 0 || (img.size() - 44) % bp) return k;
    for (std::size_t o = 44; o < img.size(); o += bp) {
        if (bits == 16) { k.push_back((std::int16_t)(std::uint16_t)(img[o] | (img[o + 1] << 8))); continue; }
        std::int64_t v = 0;
        for (std::size_t b = 0; b < bp; ++b) v |= (std::int64_t)img[o + b] << (8 * b);
        if (v & ((std::int64_t)1 << (bits - 1))) v -= (std::int64_t)1 << bits;   // sign-extend
        k.push_back(v);
    }
    return k;
}

// A mono canonical PCM image holding exactly these codes, built WITHOUT the writer (the reader's input).
static std::vector<std::uint8_t> image_of(const std::vector<long long>& k, int bits) {
    std::vector<std::uint8_t> o;
    auto u32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back((std::uint8_t)(v >> (8 * i))); };
    auto u16 = [&](std::uint16_t v) { for (int i = 0; i < 2; ++i) o.push_back((std::uint8_t)(v >> (8 * i))); };
    auto tag = [&](const char* t) { o.insert(o.end(), t, t + 4); };
    const std::uint32_t bp = (std::uint32_t)bits / 8, len = (std::uint32_t)k.size() * bp;
    tag("RIFF"); u32(36 + len); tag("WAVE");
    tag("fmt "); u32(16); u16(1); u16(1); u32(48000); u32(48000 * bp); u16((std::uint16_t)bp); u16((std::uint16_t)bits);
    tag("data"); u32(len);
    for (long long c : k) for (std::uint32_t b = 0; b < bp; ++b) o.push_back((std::uint8_t)((unsigned long long)c >> (8 * b)));
    return o;
}

// PLANTED FAILURE: the writer this shim replaced — llround on the 2^(bits-1) − 1 grid — re-encoding the
// payload of a real image (header untouched), so it is measured through exactly the same instruments.
static std::vector<std::uint8_t> old_writer(std::vector<std::uint8_t> img, const std::vector<double>& x, int bits) {
    const std::size_t bp = (std::size_t)bits / 8; const double s = full(bits) - 1.0;
    if (img.size() != 44 + x.size() * bp) return {};   // not the image asked for: fail the counts, no OOB
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double v = std::isfinite(x[i]) ? std::clamp(x[i], -1.0, 1.0) : 0.0;
        const long long c = std::llround(v * s);
        for (std::size_t b = 0; b < bp; ++b) img[44 + i * bp + b] = (std::uint8_t)((unsigned long long)c >> (8 * b));
    }
    return img;
}

// How many codes differ from what was meant (-1: wrong length — the image is not what was asked for).
static long long moved(const std::vector<long long>& got, const std::vector<long long>& want) {
    if (got.size() != want.size()) return -1;
    long long n = 0; for (std::size_t i = 0; i < got.size(); ++i) n += got[i] != want[i]; return n;
}
// How many samples wav_read returns as anything but exactly want[i] / 2^(bits-1) (-1: unreadable).
static long long read_misses(const std::string& p, const std::vector<long long>& want, int bits) {
    const WavData w = wav_read(p);
    if (!w.ok || w.ch.size() != 1 || w.frames() != want.size() || w.bits != bits || w.is_float) return -1;
    long long n = 0; for (std::size_t i = 0; i < want.size(); ++i) n += w.ch[0][i] != (double)want[i] / full(bits);
    return n;
}

static std::vector<long long> all_codes16() {
    std::vector<long long> k; for (long long c = -32768; c <= 32767; ++c) k.push_back(c); return k;
}
// 24-bit: a prime stride across the whole range + every code within 64 of 0, ±2^21, ±2^22 (where the old
// grid began to part) and of both ends.
static std::vector<long long> sample_codes24() {
    std::vector<long long> k;
    for (long long c = -8388608; c <= 8388607; c += 4099) k.push_back(c);
    for (long long ctr : { 0LL, 2097152LL, -2097152LL, 4194304LL, -4194304LL })
        for (long long d = -64; d <= 64; ++d) k.push_back(ctr + d);
    for (long long d = 0; d <= 64; ++d) { k.push_back(-8388608 + d); k.push_back(8388607 - d); }
    if (k.size() % 2) k.push_back(1);   // even count: an odd 24-bit data chunk would pull RIFF pad-byte
                                         // handling into [7]'s byte-for-byte check, which is not this test's claim
    return k;
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

    // ---- The PCM grid (the fix for the one-LSB-narrow writer) ----
    for (int bits : { 16, 24 }) {
        const auto k = bits == 16 ? all_codes16() : sample_codes24();
        std::vector<double> x; for (long long c : k) x.push_back((double)c / full(bits));
        const long long edge = 1LL << (bits - 2);
        const long long parted = (long long)std::count_if(k.begin(), k.end(), [edge](long long c) { return c > edge || c < -edge; });
        const std::string at = " (" + std::to_string(bits) + "-bit, " + std::to_string(k.size()) + " codes)";

        std::printf("[6] write∘read is the identity on every code%s\n", at.c_str());
        CHECK(wav_write(p, {x}, SR, bits, false), ("write codes k/2^(bits-1)" + at).c_str());
        const auto img = file_bytes(p);
        const auto got = codes_of(img, bits);
        CHECK(moved(got, k) == 0, ("every code k/2^(bits-1) is written as k" + at).c_str());
        CHECK(read_misses(p, k, bits) == 0, ("...and wav_read returns exactly k/2^(bits-1)" + at).c_str());

        // PLANTED FAILURE: the replaced writer, through the same two instruments — a green [6] is the
        // instruments seeing, not blind. Every |k| > 2^(bits-2) moves one LSB toward zero, nothing else.
        const auto bad = old_writer(img, x, bits);
        const auto badK = codes_of(bad, bits);
        CHECK(parted > 0 && moved(badK, k) == parted,
              ("planted: the old llround(v*(2^(bits-1)-1)) writer moves exactly the |k| > 2^(bits-2) codes" + at).c_str());
        CHECK(put_bytes(p, bad) && read_misses(p, k, bits) == parted, ("planted: ...and wav_read sees every one" + at).c_str());
        auto code_at = [&](const std::vector<long long>& v, long long c) {   // LLONG_MIN: absent / short image
            const std::size_t i = (std::size_t)(std::find(k.begin(), k.end(), c) - k.begin());
            return i < v.size() ? v[i] : std::numeric_limits<long long>::min(); };
        if (bits == 16) {
            std::printf("    old writer: %lld of 65536 codes moved; 20000 -> %lld, -32768 -> %lld\n",
                        moved(badK, k), code_at(badK, 20000), code_at(badK, -32768));
            CHECK(parted == 32767 && code_at(badK, 20000) == 19999 && code_at(badK, -32768) == -32767
                  && code_at(badK, 16385) == 16384 && code_at(badK, 16384) == 16384,
                  "planted: 32767 of 65536; 20000 -> 19999, -32768 -> -32767, 16385 -> 16384, 16384 stays");
        } else {
            CHECK(code_at(badK, 4194305) == 4194304 && code_at(badK, -8388608) == -8388607 && code_at(badK, 4194304) == 4194304,
                  "planted: 4194305 -> 4194304, -8388608 -> -8388607, 4194304 stays");
        }

        std::printf("[7] read side divides by 2^(bits-1); read∘write is byte-identical%s\n", at.c_str());
        const auto canon = image_of(k, bits);
        CHECK(put_bytes(p, canon) && read_misses(p, k, bits) == 0, ("a hand-built image reads as exactly k/2^(bits-1)" + at).c_str());
        {
            const WavData w = wav_read(p);
            const std::string p2 = "oc_wav_rt2.wav";
            CHECK(w.ok && wav_write(p2, w.ch, w.sr, bits, false) && file_bytes(p2) == canon,
                  ("...and writes back byte for byte" + at).c_str());
            std::remove(p2.c_str());
        }

        std::printf("[8] off the grid: Dither's rounding floor(v*2^(bits-1) + 1/2), clamp, NaN/Inf%s\n", at.c_str());
        {
            // ties k + 1/2 go UP on both signs (llround would take a negative tie away from zero, to k)
            std::vector<double> ties; std::vector<long long> want;
            for (long long c : k) { ties.push_back(((double)c + 0.5) / full(bits)); want.push_back(std::min(c + 1, (long long)full(bits) - 1)); }
            CHECK(wav_write(p, {ties}, SR, bits, false) && moved(codes_of(file_bytes(p), bits), want) == 0,
                  ("every tie k + 1/2 is written as k + 1, both signs" + at).c_str());

            const double f = full(bits); const long long top = (long long)f - 1, bottom = (long long)-f;
            const double inf = std::numeric_limits<double>::infinity(), nan = std::numeric_limits<double>::quiet_NaN();
            const std::vector<double> ends = { 1.0, -1.0, 1e300, -1e300, nan, inf, -inf, -0.0,
                                               std::numeric_limits<double>::denorm_min(), -std::numeric_limits<double>::denorm_min(),
                                               (f - 0.5) / f, (-f - 0.5) / f, 0.5 / f, -0.5 / f, 0.49 / f, -0.51 / f };
            const std::vector<long long> endCodes = { top, bottom, top, bottom, 0, 0, 0, 0,
                                                      0, 0,
                                                      top, bottom, 1, 0, 0, -1 };
            CHECK(wav_write(p, {ends}, SR, bits, false) && moved(codes_of(file_bytes(p), bits), endCodes) == 0,
                  ("+1.0 -> top code, -1.0 -> bottom, +-huge clamp, NaN/+-Inf -> 0, +-0/subnormals -> 0, end ties" + at).c_str());
        }
    }

    std::printf("[9] 32-bit PCM read divides by 2^31\n");
    {
        const std::vector<long long> k32 = { -2147483648LL, -2147483647LL, -1073741825LL, -1, 0, 1, 1073741825LL, 2147483646LL, 2147483647LL };
        CHECK(put_bytes(p, image_of(k32, 32)) && read_misses(p, k32, 32) == 0, "every probed 32-bit code reads as exactly k/2^31");
    }

    std::remove(p.c_str());
    std::printf(g_fail ? "\n=== %d CHECK(S) FAILED ===\n" : "\n=== ALL CHECKS PASSED ===\n", g_fail);
    return g_fail ? 1 : 0;
}
