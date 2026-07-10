// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
#include "UiSupport.h"
#include "Glyphs.h"
#include "MicHistory.h"

// One mic = one row of controls + one identity colour used across the whole scene.
// The slider is the mic's distance control when it lives off the cone (room/rear).
struct MicRowUI {
    juce::ComboBox location, mic, axis, input;
    juce::TextButton addMic, removeBtn;        // [+] add a model to the list · [x] remove the row
    juce::TextEditor dist;
    LocGlyph glyph;
    TintPanel tint { juce::Colours::orange };
    juce::Slider slider;
    MicHistory hist;                           // this mic's own 30 s level history
    juce::String position;                     // grille grid position ("" until picked)
    int slot = 0, lastLocId = 1;               // stable colour slot; last location (for limit reverts)
};
