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

// The Darwin's Cat orbit "target" mark (concentric violet/lilac/orange rings) — copied verbatim
// from orbitcab's ui/BrandMark.h so the two products render an identical mark. Placeholder logo
// until we design a dedicated OrbitCapture mark.
namespace brand {
    inline const juce::Colour violet { 0xff9778ff };   // primary accent
    inline const juce::Colour lilac  { 0xffb9a6ff };   // mid tint
    inline const juce::Colour orange { 0xffff8a3d };   // hot accent
    // Variant 08 "converging-arrows": target rings + four chevrons pointing inward to an
    // orange core — energy gathered to center = capture. (SVG viewBox 0..40, centre 20,20.)
    inline void drawOrbit(juce::Graphics& g, float cx, float cy, float d, bool hover = false) {
        const float s = d / 40.0f;
        auto X = [&](float p) { return cx + (p - 20.0f) * s; };
        auto Y = [&](float p) { return cy + (p - 20.0f) * s; };
        auto chev = [&](float ax, float ay, float bx, float by, float ex, float ey) {
            juce::Path p; p.startNewSubPath(X(ax), Y(ay)); p.lineTo(X(bx), Y(by)); p.lineTo(X(ex), Y(ey));
            g.strokePath(p, juce::PathStrokeType(2.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        g.setColour(juce::Colour(0xff0b0b11));                         // dark disc body
        g.fillEllipse(cx - 20.0f * s, cy - 20.0f * s, 40.0f * s, 40.0f * s);
        g.setColour(hover ? juce::Colour(0xff9778ff).brighter(0.2f) : juce::Colour(0xff9778ff));
        g.drawEllipse(cx - 18.5f * s, cy - 18.5f * s, 37.0f * s, 37.0f * s, 2.0f * s);   // violet outer ring
        g.setColour(juce::Colour(0xffb9a6ff));                         // lilac middle ring + chevrons
        g.drawEllipse(cx - 12.0f * s, cy - 12.0f * s, 24.0f * s, 24.0f * s, 1.6f * s);
        chev(16, 6, 20, 10, 24, 6); chev(16, 34, 20, 30, 24, 34);
        chev(6, 16, 10, 20, 6, 24); chev(34, 16, 30, 20, 34, 24);
        g.setColour(juce::Colour(0xffff8a3d));                         // orange core
        g.fillEllipse(cx - 3.5f * s, cy - 3.5f * s, 7.0f * s, 7.0f * s);
    }
}

// Fixed mic-slot palette (Fender-style: the colour IS the mic across the whole scene —
// row tint, grid dot, slider, meter badge, mixer strip).
static const juce::Colour kSlotColours[8] = {
    juce::Colour(0xffff8a3d), juce::Colour(0xff4fc3f7), juce::Colour(0xfff06292), juce::Colour(0xff81c784),
    juce::Colour(0xffb9a6ff), juce::Colour(0xffffd54f), juce::Colour(0xffe57373), juce::Colour(0xff4db6ac) };

// A gear (⚙) button that draws the glyph LARGE — sized to the button, so it reads clearly in a
// toolbar (a plain TextButton renders the glyph tiny relative to its box).
struct GearButton : juce::Button {
    GearButton() : juce::Button("gear") {}
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        g.setColour(juce::Colours::white.withAlpha(down ? 0.6f : over ? 0.95f : 0.7f));
        g.setFont(juce::FontOptions((float)juce::jmin(getWidth(), getHeight()) * 1.25f));
        g.drawText(juce::String::fromUTF8("\xe2\x9a\x99"), getLocalBounds(), juce::Justification::centred);
    }
};

// One-line text prompt (OK/Enter · Cancel/Esc). Shared by the new-session prompt, the gear-list
// managers and the add-mic-model flow.
inline void textPrompt(const juce::String& title, const juce::String& initial, std::function<void(juce::String)> onOk) {
    auto* w = new juce::AlertWindow(title, "", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor("v", initial);
    if (auto* te = w->getTextEditor("v")) {                     // make it obviously an editor (boxed, filled)
        te->setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff2b2f36));
        te->setColour(juce::TextEditor::outlineColourId, juce::Colours::grey);
        te->setColour(juce::TextEditor::focusedOutlineColourId, brand::violet);
        te->setColour(juce::TextEditor::textColourId, juce::Colours::white);
        te->setSelectAllWhenFocused(true);
    }
    w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([w, onOk](int r) {
        const juce::String v = w->getTextEditorContents("v").trim();   // strip leading/trailing space/tab/newline
        if (r == 1 && v.isNotEmpty() && onOk) onOk(v);
    }), true);
    w->toFront(true);                                           // above the (non-native) session dialog
}
