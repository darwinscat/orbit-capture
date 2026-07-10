// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
// OrbitCapture — the mic-blend engine.
//
// COMPAT SHIM (de-monolith step 4): the engine now lives in the felitronics::blend core module. This
// re-exports it into ocap:: so Main.cpp (computeBlend/renderBlendOverlay/gatherStrips) is untouched.
#include <felitronics/blend/IrBlend.h>

namespace ocap {
using felitronics::blend::anySolo;
using felitronics::blend::channelAudible;
using felitronics::blend::applyFilters;
using felitronics::blend::processedMic;
using felitronics::blend::blendIrs;
} // namespace ocap
