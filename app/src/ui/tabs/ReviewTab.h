// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
#include "ui/UiSupport.h"
#include "ui/SpectrumView.h"
#include "ui/MixStrip.h"

#include <memory>
#include <vector>

// The sample transport button: ONE play/stop — green with a ▶ when silent, red with a ■ while
// playing (the icons are drawn, not glyph text). The orchestrator flips `playing` from its timer.
struct PlayStopButton : juce::Button {
    bool playing = false;
    PlayStopButton() : juce::Button("playstop") {}
    void setPlaying(bool p) { if (playing != p) { playing = p; repaint(); } }
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        auto b = getLocalBounds().toFloat().reduced(1.0f);
        auto col = playing ? juce::Colour(0xffb63a34) : juce::Colour(0xff2f7d43);   // red = playing · green = ready
        if (down) col = col.darker(0.25f); else if (over) col = col.brighter(0.12f);
        g.setColour(col);
        g.fillRoundedRectangle(b, 4.0f);
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.drawRoundedRectangle(b, 4.0f, 1.0f);
        const auto c = b.getCentre();
        g.setColour(juce::Colours::white);                             // just the icon — it explains itself
        if (playing) g.fillRect(juce::Rectangle<float>(12.0f, 12.0f).withCentre(c));            // ■
        else { juce::Path p; p.addTriangle(c.x - 6.0f, c.y - 7.5f, c.x - 6.0f, c.y + 7.5f, c.x + 9.0f, c.y);
               g.fillPath(p); }                                                                  // ▶
    }
};

// Small drawn glyph buttons for the TAKES row — flat, hover-lit, grey stroke. Kept as a matched
// pair (plus + trash) so the two controls read as one set. `plus` = new take, `trash` = delete.
struct IconButton : juce::Button {
    enum Kind { Plus, Trash } kind;
    explicit IconButton(Kind k) : juce::Button("icon"), kind(k) {}
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        auto b = getLocalBounds().toFloat().reduced(2.0f);
        if (over || down) { g.setColour(juce::Colours::white.withAlpha(down ? 0.16f : 0.09f)); g.fillRoundedRectangle(b, 3.0f); }
        const float cx = b.getCentreX(), cy = b.getCentreY();
        g.setColour(over ? juce::Colours::white : juce::Colours::grey);
        if (kind == Plus) {
            g.drawLine(cx - 5.0f, cy, cx + 5.0f, cy, 1.6f);
            g.drawLine(cx, cy - 5.0f, cx, cy + 5.0f, 1.6f);
        } else {
            const float w = 9.0f, h = 9.0f, yc = cy + 1.0f;
            g.drawLine(cx - w * 0.6f, yc - h * 0.5f, cx + w * 0.6f, yc - h * 0.5f, 1.4f);     // lid
            g.drawLine(cx - w * 0.22f, yc - h * 0.72f, cx + w * 0.22f, yc - h * 0.72f, 1.4f); // handle
            juce::Path can;                                                                    // body
            can.startNewSubPath(cx - w * 0.45f, yc - h * 0.5f);
            can.lineTo(cx - w * 0.36f, yc + h * 0.5f);
            can.lineTo(cx + w * 0.36f, yc + h * 0.5f);
            can.lineTo(cx + w * 0.45f, yc - h * 0.5f);
            g.strokePath(can, juce::PathStrokeType(1.3f));
            for (float dx : { -0.15f, 0.15f }) g.drawLine(cx + dx * w, yc - h * 0.28f, cx + dx * w, yc + h * 0.32f, 1.0f);
        }
    }
};

// Tab "3 Mixer" — the take EDITOR. Takes on top; the console (one vertical strip per channel,
// a [+] column to append imported IRs, then Master) full-width; the response graph with the
// colour legend; and a two-row transport under it (Sample row · Live row). One channel gets the
// same console as eight — no special mono mode. Dumb view — it owns the widgets, the strip
// STORAGE and the layout; all wiring lives in the orchestrator.
struct ReviewTab : juce::Component {
    static constexpr int kStripsH = 302;       // console region height

    juce::Label takesCap;                      // "TAKES" (inline, left of the combo)
    juce::ComboBox takeBox;                    // pick a take
    IconButton importIrsBtn { IconButton::Plus };    // a new empty take (fill it via the console [+])
    IconButton deleteTakeBtn { IconButton::Trash };  // delete the current take

    juce::TextButton addChannelBtn { "+" };    // console column: append an IR to THIS take
    std::vector<std::unique_ptr<MixStrip>> mixRows;  // one strip per channel
    std::unique_ptr<MixStrip> masterStrip;           // the Master bus (gain + HPF/LPF over the sum)
    bool hasCapture = false;                         // IRs loaded — the orchestrator keeps it in sync

    SpectrumView spectrumView;
    juce::Label reviewInfo;                    // a slim status line above the transport

    // transport row 1 — the audition sample
    juce::ComboBox diBox;
    juce::TextButton loadDiBtn;
    juce::TextButton deleteSampleBtn { juce::String::fromUTF8("\xc3\x97") };   // delete the selected USER sample
    juce::ToggleButton bypassBtn { "bypass" };    // audition/monitor the DRY DI (unit-impulse IR)
    PlayStopButton playBtn;                       // one green-▶ / red-■ sample transport
    juce::ToggleButton loopToggle { "loop" };
    // transport row 2 — live input through the mix (+ record your own sample)
    juce::TextButton playLiveBtn;
    juce::ComboBox liveInBox;
    juce::TextButton recBtn { juce::String::fromUTF8("\xe2\x97\x8f Rec") };    // record the dry input into a user sample
    juce::TextButton navToExport;              // guided "-> next step"

    ReviewTab() {
        for (juce::Component* c : { (juce::Component*)&takesCap, (juce::Component*)&takeBox,
                                    (juce::Component*)&importIrsBtn, (juce::Component*)&deleteTakeBtn,
                                    (juce::Component*)&addChannelBtn,
                                    (juce::Component*)&spectrumView, (juce::Component*)&reviewInfo,
                                    (juce::Component*)&diBox, (juce::Component*)&loadDiBtn,
                                    (juce::Component*)&deleteSampleBtn,
                                    (juce::Component*)&bypassBtn, (juce::Component*)&playBtn,
                                    (juce::Component*)&loopToggle,
                                    (juce::Component*)&playLiveBtn, (juce::Component*)&liveInBox,
                                    (juce::Component*)&recBtn,
                                    (juce::Component*)&navToExport })
            addAndMakeVisible(c);
        addChannelBtn.setVisible(false);       // shown with the console
    }

    void resized() override {
        auto r = getLocalBounds().reduced(12);
        { auto a = r.removeFromTop(26);                               // ---- TAKES  [combo] [+] [trash] ----
          takesCap.setBounds(a.removeFromLeft(50));
          deleteTakeBtn.setBounds(a.removeFromRight(26)); a.removeFromRight(4);
          importIrsBtn.setBounds(a.removeFromRight(26)); a.removeFromRight(6); takeBox.setBounds(a); }
        r.removeFromTop(8);
        if (!mixRows.empty()) {                                        // ---- the console, full width ----
            auto mid = r.removeFromTop(kStripsH);
            const int n = (int)mixRows.size();
            const int stripW = juce::jlimit(58, 104, (mid.getWidth() - 44) / (n + 1));   // rarely >3 channels: keep them wide
            for (auto& mp : mixRows) { mp->setBounds(mid.removeFromLeft(stripW)); mid.removeFromLeft(4); }
            addChannelBtn.setBounds(mid.removeFromLeft(26).withTrimmedTop(kStripsH / 2 - 26).withHeight(52));
            mid.removeFromLeft(6);
            if (masterStrip) masterStrip->setBounds(mid.removeFromLeft(stripW));
            r.removeFromTop(6);
        } else if (addChannelBtn.isVisible()) {                        // a fresh named take: just the [+]
            auto mid = r.removeFromTop(64);
            addChannelBtn.setBounds(mid.removeFromLeft(26).withSizeKeepingCentre(26, 52));
            r.removeFromTop(6);
        }
        // ---- transport (bottom-up): Live row · Sample row · status line; the graph fills the rest
        auto live = r.removeFromBottom(28);
        { playLiveBtn.setBounds(live.removeFromLeft(110)); live.removeFromLeft(8);
          liveInBox.setBounds(live.removeFromLeft(200)); live.removeFromLeft(8);
          recBtn.setBounds(live.removeFromLeft(76));
          navToExport.setBounds(live.removeFromRight(150)); }
        r.removeFromBottom(4);
        auto smp = r.removeFromBottom(28);                            // sample | load | del | bypass | loop | ▶/■
        { diBox.setBounds(smp.removeFromLeft(220)); smp.removeFromLeft(6);
          loadDiBtn.setBounds(smp.removeFromLeft(76)); smp.removeFromLeft(4);
          deleteSampleBtn.setBounds(smp.removeFromLeft(24)); smp.removeFromLeft(10);
          bypassBtn.setBounds(smp.removeFromLeft(80)); smp.removeFromLeft(6);
          loopToggle.setBounds(smp.removeFromLeft(58)); smp.removeFromLeft(10);
          playBtn.setBounds(smp.removeFromLeft(64)); }                 // fixed-width ▶/■ transport
        r.removeFromBottom(4);
        reviewInfo.setBounds(r.removeFromBottom(16)); r.removeFromBottom(2);
        spectrumView.setBounds(r);                                     // the graph fills the rest
    }
};
