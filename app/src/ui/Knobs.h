// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
#include "UiSupport.h"

// Rotary knob with the value drawn INSIDE the dial (no separate text box) — used for the phase &
// shift knobs so they read big and clean on a compact strip.
struct KnobLNF : juce::LookAndFeel_V4 {
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                          float a0, float a1, juce::Slider& s) override {
        auto area = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(3.0f);
        const float d = juce::jmin(area.getWidth(), area.getHeight());
        const auto c = area.getCentre();
        const float r = d * 0.5f;
        const float ang = a0 + pos * (a1 - a0);
        const auto fill = s.findColour(juce::Slider::rotarySliderFillColourId);
        g.setColour(juce::Colour(0xff262a33)); g.fillEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f);   // dial face
        juce::Path track; track.addCentredArc(c.x, c.y, r - 2.0f, r - 2.0f, 0.0f, a0, a1, true);
        g.setColour(juce::Colour(0x33ffffff)); g.strokePath(track, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path val; val.addCentredArc(c.x, c.y, r - 2.0f, r - 2.0f, 0.0f, a0, ang, true);
        g.setColour(fill); g.strokePath(val, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const juce::Point<float> tip (c.x + std::sin(ang) * (r - 4.0f),  c.y - std::cos(ang) * (r - 4.0f));   // pointer
        const juce::Point<float> base(c.x + std::sin(ang) * (r * 0.35f), c.y - std::cos(ang) * (r * 0.35f));
        g.setColour(fill); g.drawLine({ base, tip }, 2.2f);
        g.setColour(juce::Colours::white.withAlpha(0.92f));
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText(s.getTextFromValue(s.getValue()), area.toNearestInt(), juce::Justification::centred, false);
    }
};

// A rotary knob (value drawn inside by KnobLNF) whose value can be TYPED: a single click (no drag)
// pops an inline editor centred over the dial; a double-click returns it to the default. The editor
// opens on a short delay so a double-click can pre-empt it. onCommit fires after a value is applied.
struct EditKnob : juce::Slider, private juce::Timer {
    juce::TextEditor editor;
    std::function<void()> onCommit;
    EditKnob() {
        setSliderStyle(juce::Slider::RotaryVerticalDrag);
        setDoubleClickReturnValue(true, 0.0);                        // double-click → default (native)
        addChildComponent(editor);
        editor.setJustification(juce::Justification::centred);
        editor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff11131a));
        editor.setColour(juce::TextEditor::outlineColourId, juce::Colours::grey);
        editor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
        editor.setSelectAllWhenFocused(true);
        editor.onReturnKey = [this] { commitEdit(); };
        editor.onEscapeKey = [this] { editor.setVisible(false); };
        editor.onFocusLost = [this] { commitEdit(); };
    }
    ~EditKnob() override { stopTimer(); }
    void mouseUp(const juce::MouseEvent& e) override {
        juce::Slider::mouseUp(e);                                    // a lone click → open the editor after a beat
        if (!editor.isVisible() && e.mouseWasClicked() && e.getNumberOfClicks() == 1 && e.getDistanceFromDragStart() < 4)
            startTimer(220);
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
        stopTimer();                                                 // cancel the pending editor
        juce::Slider::mouseDoubleClick(e);                          // native double-click → return to default
        if (onCommit) onCommit();
    }
    void timerCallback() override {
        stopTimer();
        editor.setText(getTextFromValue(getValue()), juce::dontSendNotification);
        editor.setBounds(getLocalBounds().withSizeKeepingCentre(juce::jmin(getWidth() - 6, 52), 18));
        editor.setVisible(true);
        editor.grabKeyboardFocus();
        editor.selectAll();
    }
    void commitEdit() {
        if (!editor.isVisible()) return;
        setValue(getValueFromText(editor.getText()), juce::sendNotificationSync);
        editor.setVisible(false);
        if (onCommit) onCommit();
    }
};
