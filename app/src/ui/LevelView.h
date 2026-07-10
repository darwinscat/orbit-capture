// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
#include "UiSupport.h"

// Visual input meter: a 30-second scrolling level history + a target band + per-channel
// mini-bars. Numbers flicker too fast to read; this shows the level at a glance and
// its trend over the last ~30 s (spot dips, drift, clips).
class LevelView : public juce::Component {
public:
    std::function<void(int)> onChannelClick;   // scratch-test binding: click a bar = bind that input
    void setBadges(std::vector<std::pair<int, juce::Colour>> b) { badges = std::move(b); }
    void mouseDown(const juce::MouseEvent& e) override {
        if (onChannelClick && !chLin.empty() && e.position.y <= 16.0f) {
            const float bw = (float)getWidth() / (float)chLin.size();
            onChannelClick(juce::jlimit(0, (int)chLin.size() - 1, (int)(e.position.x / bw)));
        }
    }
    void setChannels(const std::vector<float>& lin, int sel) {
        chLin = lin; selected = sel;
        if (chHold.size() != lin.size()) { chHold.assign(lin.size(), -120.0f); chAge.assign(lin.size(), 0); }
        for (size_t c = 0; c < lin.size(); ++c) {                  // per-channel sticky peak-hold
            const float db = lin[c] > 0.0f ? 20.0f * std::log10(lin[c]) : -120.0f;
            if (db >= chHold[c]) { chHold[c] = db; chAge[c] = 0; }
            else if (++chAge[c] > 30) chHold[c] = juce::jmax(db, chHold[c] - 0.8f);
        }
    }
    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().toFloat();
        g.fillAll(juce::Colour(0xff16181d));
        auto top = b.removeFromTop(16.0f);                        // per-channel bars: violet->orange + sticky peak
        if (!chLin.empty()) {
            const float bw = top.getWidth() / (float)chLin.size();
            for (int c = 0; c < (int)chLin.size(); ++c) {
                const float db = chLin[(size_t)c] > 0 ? 20.0f * std::log10(chLin[(size_t)c]) : -120.0f;
                const float frac = juce::jlimit(0.0f, 1.0f, juce::jmap(db, -60.0f, 0.0f, 0.0f, 1.0f));
                auto cell = juce::Rectangle<float>(top.getX() + c * bw + 1, top.getY() + 1, bw - 2, top.getHeight() - 2);
                g.setColour(juce::Colour(0xff262b32)); g.fillRect(cell);
                const float a = (c == selected) ? 1.0f : 0.4f;
                g.setGradientFill(juce::ColourGradient(juce::Colour(0xff9778ff).withAlpha(a), cell.getX(), 0.0f,
                                                       juce::Colour(0xffff8a3d).withAlpha(a), cell.getRight(), 0.0f, false));
                g.fillRect(cell.withWidth(cell.getWidth() * frac));
                g.setColour(juce::Colours::limegreen);            // green-zone notches (-18/-12)
                for (float zdb : { -18.0f, -12.0f }) {
                    const float zx = juce::jmap(zdb, -60.0f, 0.0f, 0.0f, 1.0f);
                    g.fillRect(juce::Rectangle<float>(cell.getX() + cell.getWidth() * zx - 1.0f, cell.getY(), 2.0f, cell.getHeight()));
                }
                if (c < (int)chHold.size()) {                     // sticky peak tick
                    const float pf = juce::jlimit(0.0f, 1.0f, juce::jmap(chHold[(size_t)c], -60.0f, 0.0f, 0.0f, 1.0f));
                    g.setColour(chHold[(size_t)c] >= -0.5f ? juce::Colours::red : juce::Colour(0xffb9a6ff));
                    g.fillRect(juce::Rectangle<float>(cell.getX() + cell.getWidth() * pf - 1.0f, cell.getY(), 2.0f, cell.getHeight()));
                }
                int bx = 0;                                       // mic badges (colour = bound mic)
                for (const auto& bd : badges) if (bd.first == c) {
                    g.setColour(bd.second);
                    g.fillRect(juce::Rectangle<float>(cell.getX() + 2 + (float)bx * 7.0f, cell.getBottom() - 4.0f, 5.0f, 3.0f));
                    ++bx;
                }
                g.setColour(juce::Colours::white.withAlpha(0.65f)); g.setFont(9.0f);
                g.drawText("in" + juce::String(c + 1), (int)(top.getX() + c * bw), (int)top.getY(), (int)bw, 16, juce::Justification::centred);
            }
        }
    }
private:
    std::vector<float> chLin, chHold; std::vector<int> chAge; int selected = 0;
    std::vector<std::pair<int, juce::Colour>> badges;
};
