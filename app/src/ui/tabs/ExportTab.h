// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
#include "ui/tabs/CabForm.h"
#include "core/ExportPlanner.h"

// Tab "4 Export": the cab form (quality gate) + deliverable choices + export buttons. Dumb
// view — owns the widgets and the layout; the orchestrator wires the buttons and reads
// selection() when it executes the plan.
struct ExportTab : juce::Component {
    CabForm cab;
    juce::Label exportInfo, exportStatus, authorLbl;
    juce::TextEditor authorField;              // pack author / brand for the deliverable file names
    juce::ToggleButton srcMics { "per-mic IRs" }, srcBlend { "blend (mixer settings)" }, srcRaw { "raw recordings" };
    juce::ToggleButton len1024 { "1024" }, len2048 { "2048" }, len4096 { "4096" }, len200 { "200 ms" }, len500 { "500 ms" };
    juce::ToggleButton rate44 { "44.1 kHz" }, rate48 { "48 kHz" }, rate96 { "96 kHz" };
    juce::TextButton exportBtn;
    juce::TextButton bundleBtn;                // the whole-session bundle zip

    explicit ExportTab(ListStore& ls) : cab(ls) {
        addAndMakeVisible(cab);
        exportInfo.setText("Deliverables from the last capture: per-mic IRs, the mixer MIX, and/or the raw\n"
                           "recordings, into <rate>/<length>/ folders (44.1/48/96 kHz). 24-bit PCM mono.",
                           juce::dontSendNotification);
        exportInfo.setFont(juce::FontOptions(12.0f));
        exportInfo.setColour(juce::Label::textColourId, juce::Colours::grey);
        authorLbl.setText("Author", juce::dontSendNotification);
        authorLbl.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        authorLbl.setColour(juce::Label::textColourId, brand::lilac);
        authorField.setText("Darwin's Cat", juce::dontSendNotification);
        authorField.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff2b2f36));
        authorField.setColour(juce::TextEditor::outlineColourId, juce::Colours::grey.withAlpha(0.4f));
        authorField.setColour(juce::TextEditor::focusedOutlineColourId, brand::violet);
        authorField.setColour(juce::TextEditor::textColourId, juce::Colours::white);
        authorField.setTooltip("Your name / brand — it starts every exported file name (e.g. \"Darwin's Cat - PPC212V - AKG C414 - Cap 1in.wav\").");
        for (auto* t : { &len2048, &len500, &rate44, &rate48, &rate96, &srcMics, &srcBlend })
            t->setToggleState(true, juce::dontSendNotification);       // sane defaults; 1024/200ms opt-in
        exportStatus.setJustificationType(juce::Justification::topLeft);
        for (juce::Component* c : { (juce::Component*)&exportInfo, (juce::Component*)&authorLbl,
                                    (juce::Component*)&authorField, (juce::Component*)&srcMics,
                                    (juce::Component*)&srcBlend, (juce::Component*)&srcRaw,
                                    (juce::Component*)&len1024, (juce::Component*)&len2048,
                                    (juce::Component*)&len4096, (juce::Component*)&len200,
                                    (juce::Component*)&len500, (juce::Component*)&rate44,
                                    (juce::Component*)&rate48, (juce::Component*)&rate96,
                                    (juce::Component*)&exportBtn, (juce::Component*)&bundleBtn,
                                    (juce::Component*)&exportStatus })
            addAndMakeVisible(c);
    }

    // The toggles, as the headless-tested plan's input (core/ExportPlanner.h).
    ocap::exportplan::Selection selection() const {
        ocap::exportplan::Selection s;
        s.len1024 = len1024.getToggleState(); s.len2048 = len2048.getToggleState(); s.len4096 = len4096.getToggleState();
        s.len200ms = len200.getToggleState(); s.len500ms = len500.getToggleState();
        s.rate44 = rate44.getToggleState();   s.rate48 = rate48.getToggleState();   s.rate96 = rate96.getToggleState();
        s.srcMics = srcMics.getToggleState(); s.srcBlend = srcBlend.getToggleState(); s.srcRaw = srcRaw.getToggleState();
        return s;
    }

    void resized() override {
        auto r = getLocalBounds().reduced(12);
        auto row = [&r](int h) { return r.removeFromTop(h); };
        // ---- cabinet / amp form (the quality gate; was the old "edit" dialog) ----
        cab.setBounds(row(CabForm::kHeight)); row(14);
        // ---- deliverables ----
        { auto a = row(26); authorLbl.setBounds(a.removeFromLeft(58)); a.removeFromLeft(8); authorField.setBounds(a.removeFromLeft(360)); } row(8);
        exportInfo.setBounds(row(40)); row(10);
        { auto a = row(24); srcMics.setBounds(a.removeFromLeft(130)); a.removeFromLeft(10);
          srcBlend.setBounds(a.removeFromLeft(200)); a.removeFromLeft(10); srcRaw.setBounds(a); }
        row(8);
        { auto a = row(24);
          len1024.setBounds(a.removeFromLeft(80)); len2048.setBounds(a.removeFromLeft(80)); len4096.setBounds(a.removeFromLeft(80));
          len200.setBounds(a.removeFromLeft(92)); len500.setBounds(a.removeFromLeft(92)); }
        row(8);
        { auto a = row(24);
          rate44.setBounds(a.removeFromLeft(104)); rate48.setBounds(a.removeFromLeft(96)); rate96.setBounds(a.removeFromLeft(96)); }
        row(14);
        exportBtn.setBounds(row(36).reduced(0, 2)); row(6);
        bundleBtn.setBounds(row(36).reduced(0, 2)); row(10);
        exportStatus.setBounds(r);
    }
};
