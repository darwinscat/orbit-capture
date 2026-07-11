// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// Headless tests for the RT buffer-swap discipline types (de-monolith step 8). JUCE-free.
// The one property that matters and that no UI test can see: a publish/refill NEVER moves the
// buffer's memory once it's been reserved — the RT reader holds the pointer across swaps.
#include <felitronics_test.h>
#include "audio/RtStreams.h"

#include <vector>

using felitronics::test::ok;
using felitronics::test::group;

int main()
{
    std::printf ("orbitcapture rt-streams tests\n");

    group ("AuditionStream::publish");
    {
        ocap::AuditionStream a;
        a.reserve (8);
        const float* base = a.buf.data();
        const std::vector<float> r1 { 1, 2, 3 };
        a.publish (r1.data(), r1.size());
        ok (a.playing.load() && a.len.load() == 3 && a.pos.load() == 0, "publish arms the stream");
        ok (a.buf[0] == 1.0f && a.buf[2] == 3.0f, "buffer carries the render");

        const std::vector<float> big (16, 0.5f);                       // over capacity: truncate, never grow
        a.publish (big.data(), big.size());
        ok (a.len.load() == 8 && a.buf.size() == 8, "oversized render truncates to the reserved capacity");
        ok (a.buf.data() == base, "publish never reallocates — the RT pointer stays valid");
        ok (a.buf.capacity() == 8, "capacity is monotonic");

        a.stop();
        ok (! a.playing.load(), "stop takes the flag down");
    }

    group ("ConvStream::refill");
    {
        ocap::ConvStream c;
        c.reserve (4);
        const float* base = c.buf.data();
        c.pos.store (99);
        const std::vector<float> di { 7, 8, 9, 10, 11 };
        c.refill (di.data(), di.size());                               // mode is down (0) — allowed
        ok (c.buf.size() == 4 && c.buf[0] == 7.0f, "refill truncates to capacity");
        ok (c.pos.load() == 0, "refill rewinds the read position");
        ok (c.buf.data() == base, "refill never reallocates");
        ok (c.mode.load() == 0, "refill itself never raises the mode — the caller arms it");

        c.setMode (1);
        ok (c.mode.load() == 1, "setMode arms the conv route");
        c.stop();
        ok (c.mode.load() == 0, "stop = mode 0");
    }

    group ("RecStream: the live-input recorder");
    {
        ocap::RecStream r;
        r.reserve (6);
        const float* base = r.buf.data();
        r.start();
        ok (r.recording.load() && r.len.load() == 0, "start arms and rewinds");
        for (float v : { 1.f, 2.f, 3.f }) r.push (v);
        ok (r.len.load() == 3 && r.buf[0] == 1.f && r.buf[2] == 3.f, "pushes append in order");
        for (float v : { 4.f, 5.f, 6.f, 7.f, 8.f }) r.push (v);
        ok (r.len.load() == 6 && r.full(), "capacity caps the recording (no overrun)");
        ok (r.buf.data() == base, "recording never reallocates — the RT pointer stays valid");
        r.stop();
        ok (! r.recording.load() && r.len.load() == 6, "stop keeps the recorded length");
        r.start();
        ok (r.len.load() == 0, "restart rewinds");
    }

    return felitronics::test::report();
}
