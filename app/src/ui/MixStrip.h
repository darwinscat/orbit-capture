// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
#include "UiSupport.h"
#include "Knobs.h"
#include "model/MixModel.h"   // ocap::StripParams / MasterParams — the strip reads/writes these

// Console-style fader look for the vertical gain slider: a dark groove with dB ticks and a
// classic fader cap (gradient body + an accent line across the middle, tinted the mic colour).
struct FaderLNF : juce::LookAndFeel_V4 {
    juce::Colour accent { 0xffff8a3d };
    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                          float minPos, float maxPos, juce::Slider::SliderStyle st, juce::Slider& s) override {
        if (st != juce::Slider::LinearVertical) { juce::LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, minPos, maxPos, st, s); return; }
        const float cx = (float)x + (float)w * 0.5f;
        auto track = juce::Rectangle<float>(cx - 3.0f, (float)y, 6.0f, (float)h);
        g.setColour(juce::Colour(0xff0e1013));                          // the groove
        g.fillRoundedRectangle(track, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        g.drawRoundedRectangle(track, 3.0f, 1.0f);
        auto yOfDb = [&](double db) {                                   // ticks every 6 dB across the -24..+6 range
            return juce::jmap((float)db, -24.0f, 6.0f, (float)y + (float)h, (float)y); };
        for (double db = -24.0; db <= 6.0; db += 6.0) {
            g.setColour(juce::Colours::white.withAlpha(db == 0.0 ? 0.28f : 0.10f));
            g.drawHorizontalLine((int)yOfDb(db), cx - 8.0f, cx + 8.0f); // 0 dB tick is brighter
        }
        const float cw = juce::jmin((float)w - 4.0f, 30.0f), ch = 17.0f;   // the cap
        auto cap = juce::Rectangle<float>(cx - cw * 0.5f, pos - ch * 0.5f, cw, ch);
        g.setColour(juce::Colours::black.withAlpha(0.35f));             // drop shadow
        g.fillRoundedRectangle(cap.translated(0.0f, 1.5f), 3.5f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3d434c), cap.getX(), cap.getY(),
                                               juce::Colour(0xff22262c), cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle(cap, 3.5f);
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.drawRoundedRectangle(cap, 3.5f, 1.0f);
        g.setColour(accent);                                            // the fader line
        g.fillRect(cap.getX() + 3.0f, cap.getCentreY() - 1.0f, cap.getWidth() - 6.0f, 2.0f);
    }
};

// One VERTICAL mixer channel strip — or the Master bus (isMaster). Console-style column:
// colour cap + short mic name ("C414") + context line ("in1") on top, phase/shift knobs,
// S/M and HPF/LPF button rows, and a console fader with its dB readout below (channels;
// Master has no knobs/solo/mute). The HPF/LPF are then shaped on the graph (drag the vertical
// line = cutoff, mouse-wheel = slope). ANY press anywhere on the strip — including on its
// controls — makes it the active strip immediately, and the control still works on that same
// click (the old first-click-swallowing glass is gone). The full mic description lives in the
// strip tooltip and the spectrum legend — the strip itself stays a narrow console column.
struct MixStrip : juce::Component, public juce::SettableTooltipClient {
    bool isMaster = false, active = false;
    juce::Colour colour { juce::Colours::orange };
    std::function<void()> onSelect;                  // any press → make this the active strip
    std::function<void()> onEdit;                    // double-click the header / context menu → channel editor
    std::function<void()> onDelete;                  // × / context menu → remove the channel from the take

    juce::Label name;                                // short token: "C414" / "SM57" / "Master"
    juce::Label sub;                                 // small context line under the name: "in1"
    juce::TextButton solo { "S" }, mute { "M" }, hpBtn { "HPF" }, lpBtn { "LPF" };   // enable this strip's HPF / LPF
    juce::TextButton reset { "Reset" };              // Master only: whole mixer → flat
    juce::TextButton autoBtn { "Auto" };             // Master only: auto time/polarity alignment
    juce::TextButton kill { juce::String::fromUTF8("\xc3\x97") };   // channels only: delete this channel
    juce::Slider gain;                               // console fader, dB readout below
    EditKnob phase, shift;                           // rotary knobs (channels only); single-click = type the value
    FaderLNF faderLnf;                               // per-strip: the cap's accent line = mic colour

    double hpfHz = 80.0, lpfHz = 8000.0;             // benign guitar-cab defaults (safe on an accidental enable)
    int    hpfSlopeDb = 24, lpfSlopeDb = 12;

    explicit MixStrip(bool master = false) : isMaster(master) {
        addAndMakeVisible(name);
        addAndMakeVisible(sub);
        for (auto* l : { &name, &sub }) {
            l->setInterceptsMouseClicks(false, false);
            l->setJustificationType(juce::Justification::centred);
        }
        name.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        sub.setFont(juce::FontOptions(9.0f));
        sub.setColour(juce::Label::textColourId, juce::Colours::grey);
        gain.setSliderStyle(juce::Slider::LinearVertical);           // the console fader
        gain.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 52, 15);
        gain.setSliderSnapsToMousePosition(false);                   // click on the groove selects, never jumps the value
        gain.setLookAndFeel(&faderLnf);
        for (auto* b : (isMaster ? std::initializer_list<juce::TextButton*>{ &hpBtn, &lpBtn }
                                 : std::initializer_list<juce::TextButton*>{ &solo, &mute, &hpBtn, &lpBtn }))
            { b->setClickingTogglesState(true); addAndMakeVisible(b); }
        if (isMaster) { addAndMakeVisible(reset); addAndMakeVisible(autoBtn); }   // where channels keep knobs/S/M
        else {
            kill.onClick = [this] { if (onDelete) onDelete(); };
            kill.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
            kill.setColour(juce::TextButton::textColourOffId, juce::Colours::grey);
            addAndMakeVisible(kill);                 // the orchestrator hides it on single-channel takes
        }
        for (auto* c : (isMaster ? std::initializer_list<juce::Component*>{ &gain }
                                 : std::initializer_list<juce::Component*>{ &gain, &phase, &shift }))
            addAndMakeVisible(c);
        addMouseListener(this, true);                // hear presses on CHILDREN too → select, don't swallow
    }
    ~MixStrip() override {
        gain.setLookAndFeel(nullptr);
        phase.setLookAndFeel(nullptr); shift.setLookAndFeel(nullptr);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (onSelect) onSelect();
        if (e.mods.isPopupMenu() && !isMaster) {                     // right-click anywhere on the strip
            juce::PopupMenu m;
            m.addItem(1, "Edit mic info...");
            m.addItem(2, "Delete channel", kill.isVisible());
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int r) {
                if (r == 1 && onEdit) onEdit();
                if (r == 2 && onDelete) onDelete();
            });
        }
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {      // double-click the HEADER = edit
        if (!isMaster && e.originalComponent == this && e.getEventRelativeTo(this).position.y < 34.0f && onEdit)
            onEdit();
    }
    void setActive(bool a) { if (active != a) { active = a; repaint(); } }
    void setAccent(juce::Colour c) { colour = c; faderLnf.accent = c; }
    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().toFloat().reduced(1.0f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(isMaster ? 0xff262a31 : 0xff21252c), b.getX(), b.getY(),
                                               juce::Colour(isMaster ? 0xff1d2026 : 0xff181b21), b.getX(), b.getBottom(), false));
        g.fillRoundedRectangle(b, 5.0f);
        if (active) { g.setColour(colour.withAlpha(0.09f)); g.fillRoundedRectangle(b, 5.0f); }
        g.setColour(colour.withAlpha(active ? 1.0f : 0.30f));
        g.drawRoundedRectangle(b, 5.0f, active ? 2.0f : 1.0f);
        g.setColour(colour);                                              // top colour cap
        g.fillRoundedRectangle(b.getX() + 3.0f, b.getY() + 3.0f, b.getWidth() - 6.0f, 3.5f, 1.5f);
    }
    void resized() override {
        if (!isMaster) kill.setBounds(getWidth() - 18, 7, 14, 13);   // top-right corner ×
        auto r = getLocalBounds().reduced(4);
        r.removeFromTop(6);                           // colour-cap gutter
        name.setBounds(r.removeFromTop(15));
        sub.setBounds(r.removeFromTop(11));
        r.removeFromTop(3);
        if (!isMaster) {                              // knobs stacked (value drawn inside the dial)
            const int kw = juce::jmin(r.getWidth() - 4, 46);
            phase.setBounds(r.removeFromTop(kw).withSizeKeepingCentre(kw, kw));
            r.removeFromTop(2);
            shift.setBounds(r.removeFromTop(kw).withSizeKeepingCentre(kw, kw));
            r.removeFromTop(4);
            { auto a = r.removeFromTop(17);           // S | M
              const int half = a.getWidth() / 2;
              solo.setBounds(a.removeFromLeft(half - 1)); a.removeFromLeft(2); mute.setBounds(a); }
            r.removeFromTop(3);
        } else {
            autoBtn.setBounds(r.removeFromTop(17));   // auto time/polarity alignment
            r.removeFromTop(3);
            reset.setBounds(r.removeFromTop(17));     // mixer → flat
            r.removeFromTop(3);
        }
        { auto a = r.removeFromTop(17);               // HPF | LPF
          const int half = a.getWidth() / 2;
          hpBtn.setBounds(a.removeFromLeft(half - 1)); a.removeFromLeft(2); lpBtn.setBounds(a); }
        r.removeFromTop(4);
        gain.setTextBoxStyle(juce::Slider::TextBoxBelow, false, juce::jmin(52, getWidth() - 6), 15);
        gain.setBounds(r);                            // the fader fills the rest (Master's is taller)
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
