// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
// OrbitCapture — mix parameter model.
//
// COMPAT SHIM (de-monolith step 4): the params + defaults now live in the felitronics::blend core
// module (the ratified fat-core piece; orbitcab's future blend-station reuses the same engine). This
// re-exports them into ocap:: so MixStrip / MixVar / BlendEngine + Main.cpp compile unchanged.
#include <felitronics/blend/BlendParams.h>

namespace ocap {
using felitronics::blend::Filter;
using felitronics::blend::StripParams;
using felitronics::blend::MasterParams;
using felitronics::blend::hpActiveSlope;
using felitronics::blend::lpActiveSlope;
using felitronics::blend::kHpfLo;
using felitronics::blend::kHpfHi;
using felitronics::blend::kLpfLo;
using felitronics::blend::kLpfHi;
using felitronics::blend::kShiftMsMax;
} // namespace ocap
