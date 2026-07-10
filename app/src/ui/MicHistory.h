// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
#include "UiSupport.h"

// A compact per-mic level history (~30 s): the calibration green zone + target notch +
// peak-hold, drawn in the mic's colour. One lives under EACH mic row — level is a
// property of the mic, not of the app.
class MicHistory : public juce::Component {
public:
    static constexpr int HIST = 600;   // ~30 s at the 20 Hz feed
    juce::Colour colour { 0xffff8a3d };
    std::function<void()> onClick;                                      // click the history -> select this mic
    static constexpr float kGreenDb = -21.0f, kRedDb = -9.0f;          // green / red dashed thresholds
    MicHistory() { hist.fill(-120.0f); }
    void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); }
    void push(float linPeak) {
        const float d = linPeak > 0.0f ? 20.0f * std::log10(linPeak) : -120.0f;
        hist[(size_t)head] = d; head = (head + 1) % HIST;
        cur += (d - cur) * (d > cur ? 0.6f : 0.18f);               // inertia: fast attack, slow release
        if (d >= peakHold) { peakHold = d; peakAge = 0; }          // peak-hold: stick ~1.5 s then decay
        else if (++peakAge > 30) peakHold = juce::jmax(d, peakHold - 0.8f);
        repaint();
    }
    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().toFloat();
        g.fillAll(juce::Colour(0xff16181d));
        auto meter = b.removeFromBottom(7.0f);                     // the thin live meter under the history
        b.removeFromBottom(2.0f);
        g.setColour(juce::Colour(0xff262b32)); g.fillRect(meter);
        const float mf = juce::jlimit(0.0f, 1.0f, juce::jmap(cur, -60.0f, 0.0f, 0.0f, 1.0f));
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff9778ff), meter.getX(), 0.0f,
                                               juce::Colour(0xffff8a3d), meter.getRight(), 0.0f, false));
        g.fillRect(meter.withWidth(meter.getWidth() * mf));
        { auto notch = [&](float zdb, juce::Colour c) {            // notches match the history dashed thresholds
              g.setColour(c); const float zx = juce::jmap(zdb, -60.0f, 0.0f, 0.0f, 1.0f);
              g.fillRect(juce::Rectangle<float>(meter.getX() + meter.getWidth() * zx - 1.0f, meter.getY(), 2.0f, meter.getHeight())); };
          notch(kGreenDb, juce::Colours::limegreen); notch(kRedDb, juce::Colours::red); }
        const float pf = juce::jlimit(0.0f, 1.0f, juce::jmap(peakHold, -60.0f, 0.0f, 0.0f, 1.0f));
        g.setColour(peakHold >= kRedDb ? juce::Colours::red : juce::Colour(0xffb9a6ff));   // sticky peak tick
        g.fillRect(juce::Rectangle<float>(meter.getX() + meter.getWidth() * pf - 1.0f, meter.getY(), 2.0f, meter.getHeight()));
        auto yOf = [&](float db) { return juce::jmap(juce::jlimit(-60.0f, 0.0f, db), -60.0f, 0.0f, b.getBottom(), b.getY()); };
        const float yGreen = yOf(kGreenDb), yRed = yOf(kRedDb);    // dashed thresholds (-21 / -9)
        const float H = juce::jmax(1.0f, b.getBottom() - b.getY());
        auto fB = [&](float y) { return juce::jlimit(0.001, 0.999, (double)((b.getBottom() - y) / H)); };  // 0=bottom, 1=top
        const float yGA = yOf(-32.0f), yG1 = yOf(-18.0f), yR0 = yOf(-12.0f), yR1 = yOf(-6.0f);  // colour transition anchors
        const juce::Colour grey(0xff8a8f98), green(0xff33d13f), red(0xffe0402e);
        // Fill only where the level is ABOVE -32; greens in from -32, mid at the green dash (-21),
        // full green by -18, holds to -12, green→red to -6.
        { juce::Path fill; fill.startNewSubPath(b.getX(), yGA);
          for (int i = 0; i < HIST; ++i) {
              const int idx = (head + i) % HIST;
              fill.lineTo(b.getX() + (float)i / (float)HIST * b.getWidth(), juce::jmin(yOf(hist[(size_t)idx]), yGA));
          }
          fill.lineTo(b.getRight(), yGA); fill.closeSubPath();
          juce::ColourGradient fg(green.withAlpha(0.05f), 0.0f, b.getBottom(), red.withAlpha(0.82f), 0.0f, b.getY(), false);
          fg.addColour(fB(yGA), green.withAlpha(0.06f)); fg.addColour(fB(yGreen), green.withAlpha(0.28f));
          fg.addColour(fB(yG1), green.withAlpha(0.50f)); fg.addColour(fB(yR0), green.withAlpha(0.50f));
          fg.addColour(fB(yR1), red.withAlpha(0.80f));
          g.setGradientFill(fg); g.fillPath(fill); }
        // Line: grey below -32, half-green at the green dash (-21), full green -18..-12, red above -6.
        { juce::Path line;
          for (int i = 0; i < HIST; ++i) {
              const int idx = (head + i) % HIST;
              const float x = b.getX() + (float)i / (float)HIST * b.getWidth();
              const float y = yOf(hist[(size_t)idx]);
              if (i == 0) line.startNewSubPath(x, y); else line.lineTo(x, y);
          }
          juce::ColourGradient lg(grey, 0.0f, b.getBottom(), red, 0.0f, b.getY(), false);
          lg.addColour(fB(yGA), grey);  lg.addColour(fB(yGreen), grey.interpolatedWith(green, 0.5f));
          lg.addColour(fB(yG1), green); lg.addColour(fB(yR0), green); lg.addColour(fB(yR1), red);
          g.setGradientFill(lg); g.strokePath(line, juce::PathStrokeType(1.4f)); }
        // Two dashed thresholds.
        { const float dl[] = { 5.0f, 4.0f };
          g.setColour(juce::Colours::limegreen.withAlpha(0.85f)); g.drawDashedLine(juce::Line<float>(b.getX(), yGreen, b.getRight(), yGreen), dl, 2, 1.2f);
          g.setColour(juce::Colours::red.withAlpha(0.85f));       g.drawDashedLine(juce::Line<float>(b.getX(), yRed,   b.getRight(), yRed),   dl, 2, 1.2f); }
        g.setColour(peakHold >= kRedDb ? juce::Colours::red : juce::Colours::white);
        g.setFont(10.0f);                                          // peak readout top-RIGHT (top-left holds the mic watermark)
        g.drawText(juce::String(peakHold, 1) + " dB", (int)b.getRight() - 84, (int)b.getY() + 1, 80, 12, juce::Justification::right);
    }
private:
    std::array<float, HIST> hist {};
    int head = 0, peakAge = 0; float cur = -120.0f, peakHold = -120.0f;
public:
    float peakDb() const { return peakHold; }   // for the shared calibration verdict
};
