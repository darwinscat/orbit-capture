// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>
#include <array>
#include <functional>
#include <cmath>

namespace vocab {
    static const juce::StringArray mics {                              // factory set — correct full brand names
        "Shure SM57","Shure SM58","Shure SM7B","Shure Beta 52A","Shure KSM313",
        "Sennheiser MD421","Sennheiser e906","Sennheiser e609","Sennheiser e845",
        "AKG C414","AKG C451B","AKG D112","Neumann U47 FET","Neumann U87 Ai","Neumann TLM103","Neumann KM184",
        "Beyerdynamic M160","Beyerdynamic M201 TG","Royer R-121","Audix i5","Audix D6",
        "Electro-Voice RE20","Electro-Voice RE27N/D","Heil PR40","Telefunken U47","Josephson E22s",
        "Groove Tubes MD1b FET" };
    static const juce::StringArray speakerSeed {
        "V30","G12M Greenback","G12T-75","G12H Anniversary","G12M-65 Creamback","Alnico Blue","Jensen C12N","EVM12L" };
    static const juce::StringArray positions {
        "Center Cap","Cap Edge","Center Cone","Cone Edge","Fredman","Room","Rear","Side" };
    static const juce::StringArray axis { "On-axis","Off-axis" };
    static const juce::StringArray instrument { "guitar","bass" };
    static const juce::StringArray enclosure { "cabinet","combo" };
    static const juce::StringArray speakerCount { "1","2","3","4","5","6","7","8","9" };
    static const juce::StringArray speakerSize  { "6.5","8","10","12","15","18" };   // real guitar/bass cab diameters (in)
    static const juce::StringArray ampType      { "tube","solid state","hybrid" };
    static const juce::StringArray tweeter      { "no tweeter","tweeter" };   // 3-state via "tweeter?" prompt
    static const juce::StringArray location     { "grille","room","rear" };   // mic location; grille = the one conscious default
    static const juce::StringArray room         { "untreated","treated","iso booth","bedroom","garage","live room","studio" };
    static const juce::StringArray back { "closed","open" };
    static const juce::StringArray unit { "cm","in" };
}

// The Darwin's Cat identity — PROMOTED verbatim to felitronics-appkit (Brand.h/TextPrompt.h; our
// converging-arrows drawOrbit is now THE family mark there). These are using-shims so call sites
// keep saying brand:: / kSlotColours / GearButton / textPrompt; the single home of the pixels is
// appkit — a drifted palette or mark re-brands products silently, exactly like a drifted blend
// default re-voices saved mixes.
#include <felitronics/appkit/Brand.h>
#include <felitronics/appkit/TextPrompt.h>

namespace brand = felitronics::appkit::brand;

// Fixed mic-slot palette (Fender-style: the colour IS the mic across the whole scene —
// row tint, grid dot, slider, meter badge, mixer strip).
inline const auto& kSlotColours = felitronics::appkit::brand::slotColours;

// A gear (⚙) button that draws the glyph LARGE — sized to the button (toolbar + graph overlay).
using GearButton = felitronics::appkit::brand::GearButton;

// One-line text prompt (OK/Enter · Cancel/Esc). Shared by the new-session prompt, the gear-list
// managers and the add-mic-model flow.
using felitronics::appkit::textPrompt;
