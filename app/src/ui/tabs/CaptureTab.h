// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
#include "ui/UiSupport.h"
#include "ui/Glyphs.h"
#include "ui/MicGrid.h"
#include "ui/MicRowUI.h"

#include <memory>
#include <vector>

// Tab "2 Capture": one dark scene (rear strips | speaker+grid | room sliders), the scrolling
// mic rows, the level tools and the Capture button. Dumb view — it owns the widgets + the
// mic-row STORAGE and lays them out; all wiring and placement rules live in the orchestrator
// (+ model/MicSetModel.h).
struct CaptureTab : juce::Component {
    TintPanel scenePanel { juce::Colour(0xff16181d) };      // one grey ground for sliders+speaker+grid
    MicGrid grid;
    juce::TextButton addMicBtn, captureButton, noiseButton;
    juce::Viewport micViewport;                // mic strips scroll when they overflow the fitted region
    juce::Component micHolder;                 // scroll content: all the mic strips
    HintLabel lLevelHint;
    juce::Label calibVerdict;                  // shared "all mics in the green zone" verdict during noise
    juce::Label status;
    juce::TextButton navToReview;              // guided "-> next step" (lights up after a capture)
    std::vector<std::unique_ptr<MicRowUI>> micRows;

    CaptureTab() {
        for (juce::Component* c : { (juce::Component*)&scenePanel, (juce::Component*)&grid,
                                    (juce::Component*)&addMicBtn, (juce::Component*)&micViewport,
                                    (juce::Component*)&lLevelHint, (juce::Component*)&calibVerdict,
                                    (juce::Component*)&noiseButton, (juce::Component*)&captureButton,
                                    (juce::Component*)&status, (juce::Component*)&navToReview })
            addAndMakeVisible(c);
        micViewport.setViewedComponent(&micHolder, false);     // mic strips scroll; scene + add-mic stay put
        micViewport.setScrollBarsShown(true, false);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(8);
        auto row = [&r](int h) { return r.removeFromTop(h); };
        { auto region = row(174);                              // one dark scene: rear strips | speaker+grid | room sliders
          scenePanel.setBounds(region);
          for (auto& rp : micRows) if (rp->location.getText() == "rear")
              rp->slider.setBounds(region.removeFromLeft(22).reduced(2, 10));
          region.removeFromLeft(2);
          grid.setBounds(region);
          const int sx = region.getX() + (int)(region.getHeight() * 0.80f) + 16;   // right of the speaker
          int ry = region.getY() + (int)(region.getHeight() * 0.66f);              // just under the distance labels
          for (auto& rp : micRows) if (rp->location.getText() == "room") {
              rp->slider.setBounds(sx, ry, region.getRight() - 24 - sx, 20); ry += 24; } }
        row(6);
        { auto a = row(40).reduced(0, 4);                      // Play noise | Capture, 50/50
          noiseButton.setBounds(a.removeFromLeft(a.getWidth() / 2 - 4)); a.removeFromLeft(8);
          captureButton.setBounds(a); }
        status.setBounds(row(34));                             // the result, right under the buttons
        row(6);
        { auto a = row(18); lLevelHint.setBounds(a.removeFromLeft(a.getWidth() / 2)); calibVerdict.setBounds(a); }
        row(6);
        // add-mic + "Review ->" pinned to the bottom; the mic strips fill the rest (scroll past ~4)
        { auto bot = r.removeFromBottom(26); addMicBtn.setBounds(bot.removeFromLeft(110).reduced(0, 2));
          navToReview.setBounds(bot.removeFromRight(150).reduced(0, 2)); } r.removeFromBottom(6);
        micViewport.setBounds(r);
        layoutMicHolder();
    }

    // Fit the mic strips into the viewport by height: 1 mic = full height, N mics = 1/N each,
    // down to a floor (minH); past that the holder grows taller than the viewport and scrolls.
    void layoutMicHolder() {
        const int N = (int)micRows.size();
        const int vpW = micViewport.getWidth(), vpH = micViewport.getHeight();
        if (N == 0 || vpW <= 0 || vpH <= 0) return;
        const int minH = 78;                                   // icon + combo row + a sliver of meter
        int blockH = vpH / N;
        const bool scroll = blockH < minH;
        if (scroll) blockH = minH;
        const int sbW = micViewport.getScrollBarThickness();
        const int contentW = scroll ? juce::jmax(40, vpW - sbW) : vpW;
        const int contentH = scroll ? blockH * N : vpH;
        micHolder.setSize(contentW, contentH);
        auto area = juce::Rectangle<int>(0, 0, contentW, contentH);
        for (int i = 0; i < N; ++i) {
            auto& mr = *micRows[(size_t)i];
            auto block = area.removeFromTop(blockH);
            mr.tint.setBounds(block);
            auto blk = block.reduced(4, 3);
            auto a = blk.removeFromTop(28);                    // Row 1: the form, FULL width [model][+][loc][axis][dist][input][x]
            mr.removeBtn.setBounds(a.removeFromRight(24)); a.removeFromRight(4);
            mr.input.setBounds(a.removeFromRight(96));     a.removeFromRight(4);
            mr.dist.setBounds(a.removeFromRight(46));      a.removeFromRight(4);
            mr.axis.setBounds(a.removeFromRight(88));      a.removeFromRight(4);
            mr.location.setBounds(a.removeFromRight(84));  a.removeFromRight(6);
            mr.addMic.setBounds(a.removeFromRight(26));    a.removeFromRight(4);   // [+] add-model, right of the picker
            mr.mic.setBounds(a);                                   // model fills the full width
            blk.removeFromTop(4);
            mr.hist.setBounds(blk);                                // Row 2: the level history, FULL width
        }
    }
};
