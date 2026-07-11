// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
// OrbitCapture — the RT buffer-swap discipline types, PROMOTED to core (reuse audit N4) when a
// second capture product needed them, and crew-hardened there: a zero-length publish no longer
// arms an empty looping clip (our reader's loop path would read buf[0] out of bounds), the
// recorder's len is release-published so a harvest never sees an unsynchronized sample, the
// audition buffer is fixed-size (buf.size() == capacity from reserve() on — our reader keys off
// `len`, so nothing changes), and reserve() guards int lengths. Reader rules + the known
// quiescence gap are documented in the core header — read it before touching AudioEngine.
#include <felitronics/core/RtStreams.h>

namespace ocap {

using AuditionStream = felitronics::core::AuditionStream;
using ConvStream     = felitronics::core::ConvStream;
using RecStream      = felitronics::core::RecStream;

} // namespace ocap
