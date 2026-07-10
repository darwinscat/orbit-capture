// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
#include "UiSupport.h"

// A tab page whose layout is supplied as a lambda (the controls live on CaptureComponent).
struct Page : juce::Component {
    std::function<void()> onLayout;
    void resized() override { if (onLayout) onLayout(); }
};

// The level hint with "green zone" set off in green bold (Label can't do rich text).
struct HintLabel : juce::Component {
    void paint(juce::Graphics& g) override {
        const juce::Font f(juce::FontOptions(12.0f));
        juce::AttributedString s;
        s.append("Set the mic level first: Play noise and aim for the ", f, juce::Colours::grey);
        s.append("green zone", f.boldened(), juce::Colours::limegreen);
        s.append(" (aim ~-18 dB).", f, juce::Colours::grey);
        s.draw(g, getLocalBounds().toFloat());
    }
};

// Auto shape per mic location: grille = circle, room = triangle, rear = square.
// COLOUR identifies the MIC (slot), SHAPE identifies the location. Clicking selects the row.
struct LocGlyph : juce::Component {
    juce::String loc = "grille";
    juce::Colour colour = brand::orange;
    std::function<void()> onClick;
    void set(const juce::String& l) { if (l != loc) { loc = l; repaint(); } }
    void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); }
    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().toFloat().reduced(3.0f);
        const float d = juce::jmin(b.getWidth(), b.getHeight());
        auto s = b.withSizeKeepingCentre(d, d);
        g.setColour(colour);
        if (loc == "room") {
            juce::Path p; p.addTriangle(s.getCentreX(), s.getY(), s.getX(), s.getBottom(), s.getRight(), s.getBottom());
            g.fillPath(p);
        }
        else if (loc == "rear") g.fillRect(s.reduced(1.0f));
        else g.fillEllipse(s);
    }
};

// Flat backdrop panels: the dark mic-scene panel (rear slider + speaker + grid + room slider
// share one grey ground) and the mic-row tint (colour = mic slot). Click-transparent.
struct TintPanel : juce::Component {
    juce::Colour colour;
    bool frameStyle = false, active = false;   // mic-block frame (slot colour) vs. plain solid swatch
    std::function<void()> onClick;             // set on mic-row tints to make the whole block selectable
    explicit TintPanel(juce::Colour c) : colour(c) { setInterceptsMouseClicks(false, false); }
    void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); }
    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().toFloat();
        if (frameStyle) {                       // the mic's identity: a faint fill + a slot-colour frame round the block
            auto r = b.reduced(1.0f);
            g.setColour(colour.withAlpha(active ? 0.14f : 0.06f)); g.fillRoundedRectangle(r, 5.0f);
            g.setColour(colour.withAlpha(active ? 1.0f  : 0.5f));  g.drawRoundedRectangle(r, 5.0f, active ? 2.2f : 1.3f);
        } else {
            g.setColour(colour); g.fillRoundedRectangle(b, 4.0f);
        }
    }
};

// A numbered step heading: an orange circled number + bold text.
struct StepHeading : juce::Component {
    juce::String n, text;
    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().toFloat();
        const float d = juce::jmin(b.getHeight(), 20.0f);
        g.setColour(brand::orange); g.fillEllipse(b.getX(), b.getCentreY() - d / 2, d, d);
        g.setColour(juce::Colours::white); g.setFont(juce::Font(juce::FontOptions(d * 0.62f, juce::Font::bold)));
        g.drawText(n, juce::Rectangle<float>(b.getX(), b.getCentreY() - d / 2, d, d), juce::Justification::centred);
        g.setColour(juce::Colour(0xffeef0f6)); g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        g.drawText(text, (int)(b.getX() + d + 8), 0, (int)(b.getWidth() - d - 8), (int)b.getHeight(), juce::Justification::centredLeft);
    }
};
