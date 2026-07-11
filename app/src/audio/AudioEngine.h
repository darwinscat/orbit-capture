// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
// OrbitCapture — the real-time audio callback, extracted verbatim from CaptureComponent
// (de-monolith step 7). This is the RT path: sweep playout + multi-mic record, audition
// streaming, live-conv monitor, level-set noise, meters, and the live-spectrum ring feed.
// The callback body is byte-identical to the original CaptureComponent::audioDeviceIOCallback-
// WithContext — same atomics, same memory orders, same buffer-swap discipline — only its
// enclosing class changed, plus the capture-done signal now goes through onCaptureComplete
// (RT-safe: the callee, not this struct, calls triggerAsyncUpdate()).
#include "oc/sweep.hpp"
#include "audio/RtStreams.h"

#include <felitronics/analysis/SpectrumTap.h>
#include <felitronics/convolution/ConvolutionEngine.h>

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

namespace ocap {

struct AudioEngine : public juce::AudioIODeviceCallback {
    static constexpr int kMaxMeter = 16;
    static constexpr int kMaxMics = 8;

    // ---- sweep playout / multi-mic record ----
    double sampleRate = 48000.0;
    oc::Sweep builtSweep; std::vector<double> sweep;
    int playPos = 0;
    std::atomic<bool> capturing { false };
    std::atomic<bool> noiseOn { false };
    std::uint32_t rng = 0x9e3779b9u;
    std::atomic<float> chPeak[kMaxMeter];

    // capture engine: which rows record on which channels (fixed arrays — RT reads them)
    std::array<std::vector<float>, kMaxMics> recordedM;
    int rtChans[kMaxMics] = {};
    int rtNumMics = 0, rtTotal = 0;

    // pre-rendered audition + DI-through-IR stream: the swap discipline lives in the types
    // (audio/RtStreams.h) — publish/refill only ever mutate within reserved capacity, flag down.
    AuditionStream audition;
    ConvStream conv;                              // mode: 0 off · 1 DI-through-IR · 2 live-input-through-IR

    // Play live: monitor a chosen input through the current IR — core's swap-safe, click-free
    // partitioned convolver (zero latency, warm-crossfade setIr; replaces juce::dsp::Convolution
    // and with it the whole juce_dsp dependency). setIr coalescing lives on the owner (timer).
    static constexpr int kMaxLiveIr = 1 << 18;    // ~2.7 s @ 96 kHz — covers every take length
    felitronics::convolution::ConvolutionEngine<felitronics::core::fft::DefaultRealFft, 1> liveConv;
    std::vector<float> liveScratch;
    std::atomic<int> liveChannel { 0 };
    int liveMaxBlock = 512;
    RecStream rec;                                // records the DRY live input (the user's own DI sample)

    // live spectrum analyser: the audio thread pushes the playing output into core's SpectrumTap
    // (SPSC, cache-line-split handshake — fixes the old plain-array ring's formal data race);
    // the GUI timer tryPull()s frames and FFTs them.
    felitronics::analysis::SpectrumTap spec;
    inline void specPush(float s) { spec.push(s); }

    // capture-done signal: RT-safe — this struct calls it, the owner (CaptureComponent, the
    // AsyncUpdater) triggers the async update from inside.
    std::function<void()> onCaptureComplete;

    void prepareSweep() { oc::SweepSpec s; s.sr = sampleRate; builtSweep = oc::make_sweep(s); sweep.assign(builtSweep.x.begin(), builtSweep.x.end()); }

    void audioDeviceAboutToStart(juce::AudioIODevice* d) override {
        sampleRate = d->getCurrentSampleRate();
        liveMaxBlock = juce::jmax(32, d->getCurrentBufferSizeSamples());
        liveScratch.assign((size_t)liveMaxBlock, 0.0f);
        liveConv.prepare(512, kMaxLiveIr, 512, 1);        // P=512, click-free 512-sample warm swaps
        rec.reserve((size_t)(sampleRate * 60.0));         // device stopped here — safe to (re)size
        this->spec.reset();
        prepareSweep();
    }
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut, int n,
                                          const juce::AudioIODeviceCallbackContext&) override {
        const int mch = juce::jmin(kMaxMeter, numIn);
        for (int c = 0; c < mch; ++c) { if (!in[c]) continue; float pk = chPeak[c].load(); for (int i = 0; i < n; ++i) pk = juce::jmax(pk, std::abs(in[c][i])); chPeak[c].store(pk); }
        if (!capturing.load(std::memory_order_acquire)) {
            if (audition.playing.load(std::memory_order_acquire)) {             // stream the pre-rendered audition
                int pos = audition.pos.load(); const int len = audition.len.load(); const bool loop = audition.loop.load();
                for (int i = 0; i < n; ++i) {
                    if (pos >= len) { if (loop) pos = 0; else { for (int c = 0; c < numOut; ++c) if (out[c]) out[c][i] = 0.0f; continue; } }
                    const float s = audition.buf[(size_t)pos++];
                    for (int c = 0; c < numOut; ++c) if (out[c]) out[c][i] = s;
                    specPush(s);
                }
                if (pos >= len && !loop) audition.playing.store(false, std::memory_order_release);
                audition.pos.store(pos);
                return;
            }
            const int cm = conv.mode.load(std::memory_order_acquire);          // 1 = DI clip · 2 = live input, through the IR
            if (cm != 0) {
                const int ch = liveChannel.load();
                const bool loop = audition.loop.load();
                const int dlen = (int)conv.buf.size();
                int dp = conv.pos.load();
                const int nn = juce::jmin(n, (int)liveScratch.size());
                float* mono = liveScratch.data();
                for (int i = 0; i < nn; ++i) {
                    if (cm == 1) {                                              // DI clip through the IR (live-updating audition)
                        if (dp >= dlen) { if (loop && dlen > 0) dp = 0; else { mono[i] = 0.0f; continue; } }
                        mono[i] = conv.buf[(size_t)dp++];
                    } else {                                                    // live input through the IR
                        mono[i] = (ch >= 0 && ch < numIn && in[ch]) ? in[ch][i] : 0.0f;
                        if (rec.recording.load(std::memory_order_relaxed)) rec.push(mono[i]);   // DRY, pre-conv
                    }
                }
                liveConv.process(mono, mono, nn);              // in-place, RT-safe, zero latency
                for (int i = 0; i < nn; ++i) { const float s = mono[i]; for (int c = 0; c < numOut; ++c) if (out[c]) out[c][i] = s; specPush(s); }
                for (int i = nn; i < n; ++i) for (int c = 0; c < numOut; ++c) if (out[c]) out[c][i] = 0.0f;
                if (cm == 1) { conv.pos.store(dp); if (dp >= dlen && !loop) conv.mode.store(0, std::memory_order_release); }   // ended; timer catches up
                return;
            }
            if (noiseOn.load()) {
                for (int i = 0; i < n; ++i) {
                    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                    const float ns = ((float)(rng >> 8) / 8388608.0f - 1.0f) * 0.3f;   // steady level-set noise
                    for (int c = 0; c < numOut; ++c) if (out[c]) out[c][i] = ns;
                }
            } else {
                for (int c = 0; c < numOut; ++c) if (out[c]) juce::FloatVectorOperations::clear(out[c], n);
            }
            return;
        }
        int p = playPos; const int total = rtTotal;                    // ONE sweep, ALL bound mics simultaneously
        for (int i = 0; i < n; ++i) {
            const float s = (p < (int)sweep.size()) ? (float)sweep[(size_t)p] : 0.0f;
            for (int c = 0; c < numOut; ++c) if (out[c]) out[c][i] = s;
            if (p < total)
                for (int m = 0; m < rtNumMics; ++m) {
                    const int ch = rtChans[m];
                    recordedM[(size_t)m][(size_t)p] = (ch < numIn && in[ch]) ? in[ch][i] : 0.0f;
                }
            ++p;
        }
        playPos = p;
        if (p >= total) { capturing.store(false, std::memory_order_release); if (onCaptureComplete) onCaptureComplete(); }
    }
};

} // namespace ocap
