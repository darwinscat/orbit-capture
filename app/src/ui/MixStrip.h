// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
#include "UiSupport.h"
#include "Knobs.h"
#include "model/MixModel.h"   // ocap::StripParams / MasterParams — the strip reads/writes these

// One mixer channel strip — or the Master bus (isMaster). Colour bar + name + Solo/Mute/Filter
// buttons + gain fader + big phase/shift knobs (channels only). The "F" button enables the strip's
// HPF + LPF, which are then shaped on the graph (drag the vertical line = cutoff, mouse-wheel =
// slope). A transparent glass overlay swallows the FIRST click on an inactive strip (activate only).
struct MixStrip : juce::Component {
    bool isMaster = false, active = false;
    juce::Colour colour { juce::Colours::orange };
    std::function<void()> onSelect;                  // click / interaction → make this the active strip

    juce::Label name;
    juce::TextButton solo { "S" }, mute { "M" }, hpBtn { "HPF" }, lpBtn { "LPF" };   // enable this strip's HPF / LPF
    juce::Slider gain;                               // gain fader
    EditKnob phase, shift;                           // rotary knobs (channels only); single-click = type the value

    double hpfHz = 80.0, lpfHz = 8000.0;             // benign guitar-cab defaults (safe on an accidental enable)
    int    hpfSlopeDb = 24, lpfSlopeDb = 12;

    struct Glass : juce::Component {                 // first click on an inactive strip → activate only
        std::function<void()> onClick;
        void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); }
    } glass;

    explicit MixStrip(bool master = false) : isMaster(master) {
        addAndMakeVisible(name);
        name.setInterceptsMouseClicks(false, false);                 // clicks fall through to the strip / glass
        for (auto* b : (isMaster ? std::initializer_list<juce::TextButton*>{ &hpBtn, &lpBtn }
                                 : std::initializer_list<juce::TextButton*>{ &solo, &mute, &hpBtn, &lpBtn }))
            { b->setClickingTogglesState(true); addAndMakeVisible(b); }
        for (auto* c : (isMaster ? std::initializer_list<juce::Component*>{ &gain }
                                 : std::initializer_list<juce::Component*>{ &gain, &phase, &shift }))
            addAndMakeVisible(c);
        addChildComponent(glass);                                    // added LAST → on top of the controls
        glass.onClick = [this] { if (onSelect) onSelect(); };
        glass.setVisible(!active);                                   // hittable only while inactive
    }
    ~MixStrip() override { phase.setLookAndFeel(nullptr); shift.setLookAndFeel(nullptr); }
    void mouseDown(const juce::MouseEvent&) override { if (onSelect) onSelect(); }
    void setActive(bool a) { if (active != a) { active = a; glass.setVisible(!a); repaint(); } }
    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(juce::Colour(isMaster ? 0xff23262b : 0xff1c1f26));
        g.fillRoundedRectangle(b, 5.0f);
        if (active) { g.setColour(colour.withAlpha(0.10f)); g.fillRoundedRectangle(b, 5.0f); }
        g.setColour(colour.withAlpha(active ? 1.0f : 0.30f));
        g.drawRoundedRectangle(b, 5.0f, active ? 2.0f : 1.0f);
        g.setColour(colour);                                              // left colour bar
        g.fillRoundedRectangle(b.getX() + 3.0f, b.getY() + 3.0f, 3.5f, b.getHeight() - 6.0f, 1.5f);
    }
    void resized() override {
        glass.setBounds(getLocalBounds());
        auto r = getLocalBounds().reduced(6);
        r.removeFromLeft(9);                          // colour-bar gutter
        if (!isMaster) {                              // two knobs on the right (value drawn inside the dial)
            auto knobs = r.removeFromRight(128); r.removeFromRight(8);
            const int cw = (knobs.getWidth() - 6) / 2;
            phase.setBounds(knobs.removeFromLeft(cw)); knobs.removeFromLeft(6); shift.setBounds(knobs);
        }
        auto row1 = r.removeFromTop(22);
        const int sw = 24, fw = 38, gp = 3;           // S/M · HPF/LPF buttons, right-aligned
        lpBtn.setBounds(row1.removeFromRight(fw)); row1.removeFromRight(gp);
        hpBtn.setBounds(row1.removeFromRight(fw));
        if (!isMaster) {
            row1.removeFromRight(gp); mute.setBounds(row1.removeFromRight(sw));
            row1.removeFromRight(gp); solo.setBounds(row1.removeFromRight(sw));
        }
        row1.removeFromRight(8);
        name.setBounds(row1);
        r.removeFromTop(6);
        gain.setBounds(r.removeFromTop(22));
    }

    // ---- widget <-> model bridge (de-monolith step 3): the blend engine reads these, never widgets ----
    ocap::StripParams params() const {
        return { gain.getValue(), phase.getValue(), shift.getValue(),
                 solo.getToggleState(), mute.getToggleState(),
                 { hpBtn.getToggleState(), hpfHz, hpfSlopeDb },
                 { lpBtn.getToggleState(), lpfHz, lpfSlopeDb } };
    }
    void setParams(const ocap::StripParams& p) {
        gain.setValue(p.gainDb, juce::dontSendNotification);   gain.updateText();
        phase.setValue(p.phaseDeg, juce::dontSendNotification); phase.updateText();
        shift.setValue(p.shiftMs, juce::dontSendNotification);  shift.updateText();
        solo.setToggleState(p.solo, juce::dontSendNotification);
        mute.setToggleState(p.mute, juce::dontSendNotification);
        hpBtn.setToggleState(p.hpf.on, juce::dontSendNotification); hpfHz = p.hpf.hz; hpfSlopeDb = p.hpf.slopeDb;
        lpBtn.setToggleState(p.lpf.on, juce::dontSendNotification); lpfHz = p.lpf.hz; lpfSlopeDb = p.lpf.slopeDb;
    }
    ocap::MasterParams masterParams() const {
        return { gain.getValue(), { hpBtn.getToggleState(), hpfHz, hpfSlopeDb }, { lpBtn.getToggleState(), lpfHz, lpfSlopeDb } };
    }
    void setMasterParams(const ocap::MasterParams& p) {
        gain.setValue(p.gainDb, juce::dontSendNotification); gain.updateText();
        hpBtn.setToggleState(p.hpf.on, juce::dontSendNotification); hpfHz = p.hpf.hz; hpfSlopeDb = p.hpf.slopeDb;
        lpBtn.setToggleState(p.lpf.on, juce::dontSendNotification); lpfHz = p.lpf.hz; lpfSlopeDb = p.lpf.slopeDb;
    }
};
