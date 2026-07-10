// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
// OrbitCapture — the RT buffer-swap discipline, made a type (de-monolith step 8; design §risks).
//
// The audio thread streams these buffers while a mode/playing atomic is up. The load-bearing
// rule, previously encoded only in comments at each call site:
//
//     flag DOWN  →  mutate the buffer WITHIN its reserved capacity  →  flag UP
//
// A refill that reallocates (or swaps while the flag is up) is UB the RT reader can hit
// mid-block — no unit test catches it after the fact, so the publish path lives here and
// asserts the discipline instead. std-only (atomics + vector) so it's headless-testable;
// the RT reader (AudioEngine's callback) reads the fields directly — same atomics, same
// memory orders as before the extraction.
#include <atomic>
#include <cassert>
#include <cstddef>
#include <vector>

namespace ocap {

// A pre-rendered audition clip: the message thread publishes a whole render, the RT callback
// streams it (loop-aware). `buf` must be capacity-reserved once, up front.
struct AuditionStream {
    std::vector<float> buf;
    std::atomic<bool> playing { false }, loop { false };
    std::atomic<int>  pos { 0 }, len { 0 };

    void reserve(std::size_t cap) { buf.reserve(cap); }
    void stop() { playing.store(false, std::memory_order_release); }

    // stop → assign within capacity (truncating, never reallocating) → start.
    void publish(const float* data, std::size_t n) {
        playing.store(false, std::memory_order_release);
        const std::size_t nn = n < buf.capacity() ? n : buf.capacity();
        assert(buf.capacity() >= nn);                       // capacity-monotonic: reserve() ran first
        buf.assign(data, data + (std::ptrdiff_t)nn);
        len.store((int)nn); pos.store(0);
        playing.store(true, std::memory_order_release);
    }
};

// The live-convolver input stream. mode: 0 off · 1 DI clip through the IR · 2 live input
// through the IR. `buf`/`pos` are only read in mode 1; the mode atomic also gates the
// convolver + live-input path, so it stays the single flag for the whole conv route.
struct ConvStream {
    std::vector<float> buf;
    std::atomic<int> mode { 0 };
    std::atomic<int> pos { 0 };

    void reserve(std::size_t cap) { buf.reserve(cap); }
    void setMode(int m) { mode.store(m, std::memory_order_release); }
    void stop() { setMode(0); }

    // Refill the DI clip. The caller must have taken the mode DOWN first (stop()) — the RT
    // side may otherwise stream the buffer mid-assign. Truncates to the reserved capacity.
    void refill(const float* data, std::size_t n) {
        assert(mode.load(std::memory_order_acquire) == 0);  // never mutate under a live reader
        const std::size_t nn = n < buf.capacity() ? n : buf.capacity();
        buf.assign(data, data + (std::ptrdiff_t)nn);
        pos.store(0);
    }
};

} // namespace ocap
