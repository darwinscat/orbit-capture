// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
#include "ui/UiSupport.h"
#include "ui/SpectrumView.h"
#include "ui/MixStrip.h"

#include <memory>
#include <vector>

// Tab "3 Review": takes · audition sample · live test · the blend mixer + response graph. Dumb view — it
// owns the widgets, the mixer-strip STORAGE and the layout; all wiring (audition, strips, saves)
// lives in the orchestrator, which flips `hasCapture` when IRs arrive/leave.
struct ReviewTab : juce::Component {
    juce::ComboBox takeBox;                    // pick a take
    juce::TextButton deleteTakeBtn;
    juce::TextButton editMicsBtn { "Edit mics" };
    juce::TextButton importIrsBtn { "+ IRs" }; // import already-captured IR files as a new take
    juce::ComboBox diBox;
    juce::TextButton loadDiBtn;
    juce::ToggleButton bypassBtn { "bypass" };    // audition/monitor the DRY DI (unit-impulse IR)
    juce::TextButton playWetBtn, stopBtn;
    juce::ToggleButton loopToggle { "loop" };
    juce::Label reviewInfo;                    // a slim status line above the mixer
    // Play live: monitor a chosen input through the current IR (RT-safe partitioned convolution).
    juce::TextButton playLiveBtn;
    juce::ComboBox liveInBox;
    juce::TextButton analyzerBtn { "spectrum" };     // pressed-in = live analyser overlay on (default); sits by Reset
    juce::TextButton resetMixBtn { "Reset" };        // mixer -> flat: 0 dB, no phase, filters off, no solo/mute
    juce::TextButton monoFilterBtn { "F" };          // single-mic: enable HPF/LPF (shaped on the graph, no strip)
    juce::Label mixerCaption;
    juce::Label auditionCap, takesCap, liveCap;   // section captions: TAKES · AUDITION SAMPLE · LIVE TEST
    SpectrumView spectrumView;
    juce::TextButton navToExport;              // guided "-> next step"

    std::vector<std::unique_ptr<MixStrip>> mixRows;  // one channel strip per mic (multi-mic takes)
    std::unique_ptr<MixStrip> masterStrip;           // the Master bus (gain + HPF/LPF over the sum)
    bool hasCapture = false;                         // IRs loaded — the orchestrator keeps it in sync

    ReviewTab() {
        for (juce::Component* c : { (juce::Component*)&takeBox, (juce::Component*)&deleteTakeBtn,
                                    (juce::Component*)&importIrsBtn,
                                    (juce::Component*)&editMicsBtn, (juce::Component*)&diBox,
                                    (juce::Component*)&loadDiBtn, (juce::Component*)&bypassBtn,
                                    (juce::Component*)&playWetBtn, (juce::Component*)&stopBtn,
                                    (juce::Component*)&loopToggle, (juce::Component*)&reviewInfo,
                                    (juce::Component*)&spectrumView, (juce::Component*)&takesCap,
                                    (juce::Component*)&auditionCap, (juce::Component*)&liveCap,
                                    (juce::Component*)&mixerCaption, (juce::Component*)&resetMixBtn,
                                    (juce::Component*)&playLiveBtn, (juce::Component*)&liveInBox,
                                    (juce::Component*)&analyzerBtn, (juce::Component*)&monoFilterBtn,
                                    (juce::Component*)&navToExport })
            addAndMakeVisible(c);
        monoFilterBtn.setVisible(false);
        resetMixBtn.setVisible(false); mixerCaption.setVisible(false); analyzerBtn.setVisible(false);
        editMicsBtn.setEnabled(false);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(12);
        takesCap.setBounds(r.removeFromTop(15)); r.removeFromTop(2);   // ---- 1) TAKES ----
        { auto a = r.removeFromTop(26);
          deleteTakeBtn.setBounds(a.removeFromRight(28)); a.removeFromRight(6);
          editMicsBtn.setBounds(a.removeFromRight(84)); a.removeFromRight(6);
          importIrsBtn.setBounds(a.removeFromRight(64)); a.removeFromRight(6); takeBox.setBounds(a); }
        r.removeFromTop(10);
        auditionCap.setBounds(r.removeFromTop(15)); r.removeFromTop(2); // ---- 2) AUDITION SAMPLE ----
        { auto a = r.removeFromTop(32);                                // sample | Load | bypass | Play .... | Stop | loop
          diBox.setBounds(a.removeFromLeft(230)); a.removeFromLeft(8);
          loadDiBtn.setBounds(a.removeFromLeft(96)); a.removeFromLeft(16);
          bypassBtn.setBounds(a.removeFromLeft(88)); a.removeFromLeft(8);
          loopToggle.setBounds(a.removeFromRight(60)); a.removeFromRight(6);
          stopBtn.setBounds(a.removeFromRight(88)); a.removeFromRight(8);
          playWetBtn.setBounds(a); }                                   // Play fills the middle
        r.removeFromTop(10);
        liveCap.setBounds(r.removeFromTop(15)); r.removeFromTop(2);    // ---- 3) LIVE TEST ----
        { auto a = r.removeFromTop(30); playLiveBtn.setBounds(a.removeFromLeft(150)); a.removeFromLeft(10);
          liveInBox.setBounds(a.removeFromLeft(240)); }
        r.removeFromTop(8);
        reviewInfo.setBounds(r.removeFromTop(16)); r.removeFromTop(4);  // slim status line (playing / errors)
        if (!mixRows.empty()) {                                    // the mixer (multi-mic sets only)
            r.removeFromTop(8);
            { auto a = r.removeFromTop(20);                        // caption ...... [spectrum] [Reset]
              mixerCaption.setBounds(a.removeFromLeft(72).withSizeKeepingCentre(72, 18));
              const auto rslot = a.removeFromRight(64).withSizeKeepingCentre(62, 18); a.removeFromRight(8);
              resetMixBtn.setBounds(rslot);
              monoFilterBtn.setBounds(rslot.withSizeKeepingCentre(28, 18));   // same slot; Reset (multi) / F (mono) are exclusive
              analyzerBtn.setBounds(a.removeFromRight(88).withSizeKeepingCentre(88, 18)); }
            r.removeFromTop(4);
            const int stripH = 62;                                 // slimmer strips (smaller phase/shift dials)
            for (auto& mp : mixRows) { mp->setBounds(r.removeFromTop(stripH)); r.removeFromTop(4); }
            if (masterStrip) { r.removeFromTop(2); masterStrip->setBounds(r.removeFromTop(stripH)); }
        } else if (hasCapture) {                                  // single mic: no strip, just the F (HPF/LPF) toggle + analyser
            r.removeFromTop(8);
            auto a = r.removeFromTop(20);
            monoFilterBtn.setBounds(a.removeFromLeft(40).withSizeKeepingCentre(38, 18));
            analyzerBtn.setBounds(a.removeFromRight(88).withSizeKeepingCentre(88, 18));
            r.removeFromTop(4);
        }
        r.removeFromTop(10);
        navToExport.setBounds(r.removeFromBottom(30).removeFromRight(160).reduced(0, 2)); r.removeFromBottom(6);  // -> next step
        spectrumView.setBounds(r.reduced(0, 2));                   // the EQ curve / blend overlay fills the rest
    }
};
