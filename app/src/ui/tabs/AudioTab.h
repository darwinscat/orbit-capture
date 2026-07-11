// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
#include "ui/UiSupport.h"

// Tab "1 Audio": the device selector, the opt-in update-check button (bottom-left; the version
// readout lives in the header), and the guided "-> next step" button. Dumb view — the orchestrator
// creates the selector (it needs the AudioDeviceManager) and wires the buttons.
struct AudioTab : juce::Component {
    juce::Component* selector = nullptr;       // owned by the orchestrator; laid out here
    juce::TextButton navToCapture;
    juce::TextButton updateBtn;                // "check for updates" / "update available -> vX.Y.Z"

    AudioTab() {
        addAndMakeVisible(navToCapture);
        addAndMakeVisible(updateBtn);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(8);
        if (selector) selector->setBounds(r.removeFromTop(240));
        auto bottom = r.removeFromBottom(34);
        navToCapture.setBounds(bottom.removeFromRight(170).reduced(0, 2));   // -> next step
        updateBtn.setBounds(bottom.removeFromLeft(190).reduced(0, 2));
    }
};
