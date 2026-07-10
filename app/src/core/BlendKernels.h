// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
// OrbitCapture — blend DSP kernels.
//
// COMPAT SHIM (de-monolith step 4): the kernels now live VERBATIM in the felitronics::blend core
// module (byte-identical numerics — see the frozen-legacy byte-NULL in app/tests). Re-export into ocap::.
#include <felitronics/blend/BlendKernels.h>

namespace ocap {
using felitronics::blend::biquadInplace;
using felitronics::blend::onePoleInplace;
using felitronics::blend::applyBlendSlope;
using felitronics::blend::rotatePhase;
using felitronics::blend::shiftFrac;
} // namespace ocap
