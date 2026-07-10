// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
#include "ui/UiSupport.h"

// Tab "1 Audio": the device selector + the guided "-> next step" button. Dumb view — the
// orchestrator creates the selector (it needs the AudioDeviceManager) and wires the button.
struct AudioTab : juce::Component {
    juce::Component* selector = nullptr;       // owned by the orchestrator; laid out here
    juce::TextButton navToCapture;

    AudioTab() { addAndMakeVisible(navToCapture); }

    void resized() override {
        auto r = getLocalBounds().reduced(8);
        if (selector) selector->setBounds(r.removeFromTop(240));
        navToCapture.setBounds(r.removeFromBottom(34).removeFromRight(170).reduced(0, 2));   // -> next step
    }
};
