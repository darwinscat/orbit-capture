// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
#include "UiSupport.h"
#include <span>
#include <felitronics/analysis/offline/SpectrumCurve.h>   // spectrum display curves (core)

// Magnitude spectrum of the last captured IR (log-f 20..20k, dB, 1/12-oct smoothed) — the
// cabinet's EQ curve at a glance. Phase B grows this into the blend overlay: per-mic curves
// in mic colours + the blend + interference tint (where phase eats frequencies).
class SpectrumView : public juce::Component {
public:
    static constexpr int NP = 256;
    struct Trace { std::vector<double> curve; juce::Colour colour; float thick; bool fill; };
    SpectrumView() { setMouseCursor(juce::MouseCursor::CrosshairCursor); }
    void mouseMove(const juce::MouseEvent& e) override { mx = (float)e.position.x; my = (float)e.position.y; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { mx = -1.0f; repaint(); }

    // ---- the active strip's HPF/LPF as two draggable vertical lines (wheel over a line = slope) ----
    static constexpr int kSlopes[7] = { 6, 12, 24, 36, 48, 72, 96 };   // discrete dB/oct steps
    static int slopeIdxOfDb(int db) { for (int i = 0; i < 7; ++i) if (kSlopes[i] == db) return i; return 2; }

    static constexpr double kHpMin = 20.0, kHpMax = 2000.0, kLpMin = 500.0, kLpMax = 20000.0;
    std::function<void(double, int, double, int)> onFilterDrag; // live → (hpHz, hpSlopeDb, lpHz, lpSlopeDb)
    std::function<void()> onFilterDragEnd;                      // release / wheel → persist
    std::function<void(int)> onPickTrace;                       // click a curve → select its strip (the composite = Master)
    std::function<void(int)> onPickLegend;                      // click a legend row → its entry index
    std::function<void(juce::Rectangle<int>)> onGearMenu;       // click the gear → open the view menu (screen coords)

    // The colour → full-description legend (top-right). Entry order is the caller's contract
    // (mics first, MIX last); `active` bolds the selected row, `dim` greys a muted/off-solo
    // channel (its curve leaves the graph — the legend is where it remains visible). Empty
    // list hides the legend.
    struct LegendEntry { juce::String text; juce::Colour colour; bool dim = false; };
    void setLegend(std::vector<LegendEntry> e, int active) { legend = std::move(e); legendActive = active; repaint(); }

    void setActiveFilter(bool active, bool hpOn, bool lpOn, double hpHz, int hpDb, double lpHz, int lpDb, juce::Colour col) {
        fltActive = active; fltHp = hpOn; fltLp = lpOn; hpF = hpHz; hpDb_ = hpDb; lpF = lpHz; lpDb_ = lpDb; lineColour = col; repaint();
    }
    int lineHit(float x) const {                                // 1 = hp line, 2 = lp line, 0 = none (only enabled lines)
        if (!fltActive) return 0;
        const float dh = fltHp ? std::abs(x - xOfFreq(hpF)) : 1.0e9f;
        const float dl = fltLp ? std::abs(x - xOfFreq(lpF)) : 1.0e9f;
        if (dh <= 7.0f && dh <= dl) return 1;
        if (dl <= 7.0f)             return 2;
        return 0;
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (gearRect().contains(e.position.toInt())) {           // the view-options gear (top-left overlay)
            if (onGearMenu) onGearMenu(localAreaToGlobal(gearRect())); return; }
        for (int i = 0; i < (int)legendRects.size(); ++i)       // legend rows take priority (they sit on top)
            if (legendRects[(size_t)i].contains(e.position.toInt())) { if (onPickLegend) onPickLegend(i); return; }
        drag = lineHit((float)e.position.x);                    // 1 hp-line · 2 lp-line · 0 none
        if (drag != 0) { dragStartY = (float)e.position.y; dragStartSlope = slopeIdxOfDb(drag == 1 ? hpDb_ : lpDb_); }
        else if (onPickTrace) { const int t = nearestTrace(e.position); if (t >= 0) onPickTrace(t); }
    }
    void mouseDrag(const juce::MouseEvent& e) override {         // X = cutoff · Y (up = steeper) = slope
        if (drag == 0) return;
        if (drag == 1) hpF = juce::jlimit(kHpMin, std::min(kHpMax, lpF * 0.5), freqAtX((float)e.position.x));
        else           lpF = juce::jlimit(std::max(kLpMin, hpF * 2.0), kLpMax, freqAtX((float)e.position.x));
        const int idx = juce::jlimit(0, 6, dragStartSlope + (int)std::lround(((float)e.position.y - dragStartY) / kPxPerStep));   // drag up = shallower (curve rises)
        (drag == 1 ? hpDb_ : lpDb_) = kSlopes[idx];
        if (onFilterDrag) onFilterDrag(hpF, hpDb_, lpF, lpDb_);
    }
    void mouseUp(const juce::MouseEvent&) override { if (drag) { drag = 0; if (onFilterDragEnd) onFilterDragEnd(); } }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        if (!fltActive || (!fltHp && !fltLp) || w.deltaY == 0.0f) return;   // wheel over the nearer enabled line steps its slope
        wheelAccum += (w.isReversed ? -w.deltaY : w.deltaY);    // accumulate — one step per full notch (less twitchy)
        if (std::abs(wheelAccum) < kWheelStep) return;
        const int dir = wheelAccum > 0.0f ? 1 : -1;
        wheelAccum -= (float)dir * kWheelStep;                  // keep the remainder
        const float x = (float)e.position.x;
        const bool onHp = (fltHp && fltLp) ? (std::abs(x - xOfFreq(hpF)) <= std::abs(x - xOfFreq(lpF))) : fltHp;
        int& db = onHp ? hpDb_ : lpDb_;
        db = kSlopes[juce::jlimit(0, 6, slopeIdxOfDb(db) + dir)];
        if (onFilterDrag) onFilterDrag(hpF, hpDb_, lpF, lpDb_);
        if (onFilterDragEnd) onFilterDragEnd();
        repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {  // double-click a filter line → back to its default
        const int h = lineHit((float)e.position.x);
        if (h == 0) return;
        if (h == 1) { hpF = 80.0; hpDb_ = 24; } else { lpF = 8000.0; lpDb_ = 12; }
        if (onFilterDrag) onFilterDrag(hpF, hpDb_, lpF, lpDb_);
        if (onFilterDragEnd) onFilterDragEnd();
        repaint();
    }
    static juce::String freqLabel(double f) {
        return f >= 1000.0 ? juce::String(f / 1000.0, f >= 10000.0 ? 1 : 2) + "k" : juce::String((int)std::lround(f));
    }

    // Log-spaced (20..20k), 1/12-octave-smoothed magnitude curve in dB. `normalize` peaks at 0 dB;
    // pass false when several curves must share one reference (the blend overlay).
    // v0.7.0: delegates to felitronics::analysis::offline (core owns the curve math; defaults = the
    // former 20 Hz–20 kHz / 256-pt / 1/12-oct behaviour).
    static std::vector<double> makeCurve(const std::vector<float>& ir, double sr, bool normalize = true) {
        namespace off = felitronics::analysis::offline;
        off::LogCurveSpec s; s.points = NP; s.normalize = normalize;
        return off::logMagnitudeCurve(std::span<const float>(ir), sr, s);
    }
    void setIR(const std::vector<float>& ir, double sr) {
        traces.clear(); interference.clear();
        auto c = makeCurve(ir, sr);
        if (!c.empty()) traces.push_back({ std::move(c), juce::Colour(0xff9778ff), 1.6f, true });
        repaint();
    }
    // Blend overlay: thin per-mic curves + a thick blend curve; `interf` (dB) tints the columns
    // where phase eats (red) or reinforces (green) relative to the incoherent power sum.
    void setTraces(std::vector<Trace> t, std::vector<double> interf) {
        traces = std::move(t); interference = std::move(interf); repaint();
    }
    // Live spectrum analyser of the playing audio (audition / live monitor), overlaid on the curve.
    void setLiveSpectrum(const std::vector<double>& c) {
        if (c.empty()) { if (!liveSpec.empty()) { liveSpec.clear(); repaint(); } return; }
        if (liveSpec.size() != c.size()) liveSpec.assign(c.size(), -120.0);
        for (size_t i = 0; i < c.size(); ++i) liveSpec[i] += (c[i] - liveSpec[i]) * 0.4;   // temporal smoothing
        repaint();
    }
    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().toFloat();
        g.fillAll(juce::Colour(0xff16181d));
        auto xOf = [&](double f)  { return b.getX() + (float)(std::log(f / 20.0) / std::log(1000.0)) * b.getWidth(); };
        auto yOf = [&](double db) { return juce::jmap((float)db, -48.0f, 6.0f, b.getBottom(), b.getY()); };
        if (!interference.empty()) {                                   // phase-interaction tint (behind everything)
            const float cw = b.getWidth() / (float)interference.size();
            for (int i = 0; i < (int)interference.size(); ++i) {
                const double v = interference[(size_t)i];
                if (v < -3.0)      g.setColour(juce::Colours::red.withAlpha((float)juce::jlimit(0.05, 0.30, (-v - 3.0) / 20.0)));
                else if (v > 1.5)  g.setColour(juce::Colours::limegreen.withAlpha(0.06f));
                else continue;
                g.fillRect(juce::Rectangle<float>(b.getX() + (float)i * cw, b.getY(), cw + 0.5f, b.getHeight()));
            }
        }
        g.setFont(9.0f);
        for (double f : { 100.0, 1000.0, 10000.0 }) {
            g.setColour(juce::Colours::white.withAlpha(0.08f));
            g.drawVerticalLine((int)xOf(f), b.getY(), b.getBottom());
            g.setColour(juce::Colours::grey);
            g.drawText(f >= 1000.0 ? juce::String((int)(f / 1000.0)) + "k" : juce::String((int)f),
                       (int)xOf(f) + 2, (int)b.getBottom() - 12, 30, 12, juce::Justification::left);
        }
        for (double db : { 0.0, -12.0, -24.0, -36.0 }) {
            g.setColour(juce::Colours::white.withAlpha(db == 0.0 ? 0.15f : 0.08f));
            g.drawHorizontalLine((int)yOf(db), b.getX(), b.getRight());
        }
        if (traces.empty()) {
            g.setColour(juce::Colours::grey);
            g.drawText("Capture or test-run to see the cab's frequency curve", getLocalBounds(), juce::Justification::centred);
            return;
        }
        for (const auto& t : traces) {
            juce::Path p;
            for (int i = 0; i < (int)t.curve.size(); ++i) {
                const float x = b.getX() + (float)i / (float)(t.curve.size() - 1) * b.getWidth();
                const float y = yOf(juce::jlimit(-60.0, 12.0, t.curve[(size_t)i]));
                if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
            }
            g.setColour(t.colour); g.strokePath(p, juce::PathStrokeType(t.thick));
            if (t.fill) {
                auto fill = p;
                fill.lineTo(b.getRight(), b.getBottom()); fill.lineTo(b.getX(), b.getBottom()); fill.closeSubPath();
                g.setColour(t.colour.withAlpha(0.15f)); g.fillPath(fill);
            }
        }
        if (!liveSpec.empty()) {                                       // live spectrum analyser — line only, over the curve
            juce::Path sp;
            for (int i = 0; i < (int)liveSpec.size(); ++i) {
                const float x = b.getX() + (float)i / (float)(liveSpec.size() - 1) * b.getWidth();
                const float y = yOf(juce::jlimit(-60.0, 12.0, liveSpec[(size_t)i]));
                if (i == 0) sp.startNewSubPath(x, y); else sp.lineTo(x, y);
            }
            g.setColour(juce::Colour(0xff5ad1ff).withAlpha(0.6f)); g.strokePath(sp, juce::PathStrokeType(1.0f));
        }
        if (fltActive && (fltHp || fltLp)) {                           // the strip's HPF/LPF: draggable vertical line per enabled side
            const float xh = b.getX() + xOfFreq(hpF), xl = b.getX() + xOfFreq(lpF);
            g.setColour(juce::Colours::black.withAlpha(0.28f));        // shade the enabled stopband(s)
            if (fltHp) g.fillRect(juce::Rectangle<float>(b.getX(), b.getY(), xh - b.getX(), b.getHeight()));
            if (fltLp) g.fillRect(juce::Rectangle<float>(xl, b.getY(), b.getRight() - xl, b.getHeight()));
            {                                                         // the filter's own EQ response curve (0 dB → rolloff)
                juce::Path eq;
                const int NE = 240;
                for (int i = 0; i <= NE; ++i) {
                    const double f = 20.0 * std::pow(1000.0, (double)i / NE);
                    const double nh = hpDb_ / 3.0, nl = lpDb_ / 3.0;   // Butterworth order n = slope/6
                    double mag2 = 1.0;
                    if (fltHp) mag2 *= 1.0 / (1.0 + std::pow(hpF / f, nh));
                    if (fltLp) mag2 *= 1.0 / (1.0 + std::pow(f / lpF, nl));
                    const double db = 10.0 * std::log10(mag2 + 1e-12);
                    const float x = b.getX() + (float)i / (float)NE * b.getWidth();
                    const float y = yOf(juce::jlimit(-48.0, 6.0, db));
                    if (i == 0) eq.startNewSubPath(x, y); else eq.lineTo(x, y);
                }
                g.setColour(lineColour.withAlpha(0.85f));
                g.strokePath(eq, juce::PathStrokeType(1.6f));
            }
            auto line = [&](float x, const juce::String& label, bool leftLabel) {
                g.setColour(lineColour);
                g.drawVerticalLine((int)x, b.getY(), b.getBottom());
                g.fillRoundedRectangle(x - 2.0f, b.getCentreY() - 16.0f, 4.0f, 32.0f, 2.0f);   // grab tab
                g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
                g.drawText(label, (int)(leftLabel ? x - 72 : x + 6), (int)b.getBottom() - 30, 66, 12,   // low, clear of the cursor readout
                           leftLabel ? juce::Justification::right : juce::Justification::left);
            };
            if (fltHp) line(xh, "HPF " + freqLabel(hpF) + " " + juce::String(hpDb_), false);
            if (fltLp) line(xl, "LPF " + freqLabel(lpF) + " " + juce::String(lpDb_), true);
        }
        if (!traces.empty()) {                                         // the view-options gear (top-left overlay)
            const bool hover = mx >= 0.0f && gearRect().contains(juce::Point<int>((int)mx, (int)my));
            g.setColour(juce::Colours::white.withAlpha(hover ? 0.9f : 0.35f));
            g.setFont(juce::FontOptions(15.0f));
            g.drawText(juce::String::fromUTF8("\xe2\x9a\x99"), gearRect(), juce::Justification::centred);
        }
        legendRects.clear();
        if (!legend.empty()) {                                         // colour → mic legend, top-right, clickable
            const juce::Font lf(juce::FontOptions(10.0f));
            int wMax = 0;
            for (const auto& e : legend) wMax = juce::jmax(wMax, (int)std::ceil(juce::GlyphArrangement::getStringWidth(lf, e.text)));
            const int rowH = 14, pad = 6, chip = 8;
            const int pw = juce::jmin((int)b.getWidth() - 20, wMax + chip + 3 * pad);
            auto panel = juce::Rectangle<int>((int)b.getRight() - pw - 8, (int)b.getY() + 6, pw, rowH * (int)legend.size() + pad);
            g.setColour(juce::Colours::black.withAlpha(0.45f));
            g.fillRoundedRectangle(panel.toFloat(), 4.0f);
            auto rows = panel.reduced(pad, pad / 2);
            g.setFont(lf);
            for (int i = 0; i < (int)legend.size(); ++i) {
                auto row = rows.removeFromTop(rowH);
                legendRects.push_back(row.expanded(2, 0));
                const bool act = i == legendActive, dim = legend[(size_t)i].dim;
                g.setColour(legend[(size_t)i].colour.withAlpha(dim ? 0.3f : act ? 1.0f : 0.8f));
                g.fillRoundedRectangle((float)row.getX(), (float)row.getCentreY() - chip / 2.0f, (float)chip, (float)chip, 2.0f);
                if (act) g.drawRoundedRectangle((float)row.getX() - 1.5f, row.getCentreY() - chip / 2.0f - 1.5f, chip + 3.0f, chip + 3.0f, 3.0f, 1.0f);
                g.setColour(juce::Colours::white.withAlpha(dim ? 0.35f : act ? 1.0f : 0.72f));
                g.setFont(juce::FontOptions(10.0f, act ? juce::Font::bold : juce::Font::plain));
                g.drawText(legend[(size_t)i].text, row.withTrimmedLeft(chip + pad), juce::Justification::centredLeft);
            }
        }
        const auto& main = traces.back().curve;                        // crosshair reads the main (last) trace
        if (mx >= b.getX() && mx <= b.getRight() && !main.empty()) {
            const float rel = (mx - b.getX()) / b.getWidth();
            const double f  = 20.0 * std::pow(1000.0, (double)rel);
            const int    i  = juce::jlimit(0, (int)main.size() - 1, (int)std::lround(rel * (float)(main.size() - 1)));
            const double db = main[(size_t)i];
            g.setColour(juce::Colours::white.withAlpha(0.35f));
            g.drawVerticalLine((int)mx, b.getY(), b.getBottom());
            const float y = yOf(juce::jlimit(-60.0, 12.0, db));
            g.setColour(juce::Colour(0xffff8a3d));
            g.fillEllipse(mx - 3.0f, y - 3.0f, 6.0f, 6.0f);            // marker riding the curve
            const juce::String txt = (f >= 1000.0 ? juce::String(f / 1000.0, 2) + " kHz" : juce::String((int)std::lround(f)) + " Hz")
                                   + "   " + juce::String(db, 1) + " dB";
            const int tw = 120;
            const bool flip = mx > b.getRight() - (float)tw - 10.0f;
            g.setFont(11.0f); g.setColour(juce::Colours::white);
            g.drawText(txt, (int)mx + (flip ? -tw - 6 : 6), (int)b.getY() + 4, tw, 14,
                       flip ? juce::Justification::right : juce::Justification::left);
        }
    }
private:
    int nearestTrace(juce::Point<float> p) const {                 // the mic curve closest to the click (dB distance)
        if (traces.empty()) return -1;
        const int i = juce::jlimit(0, NP - 1, (int)std::lround((std::log(freqAtX(p.x) / 20.0) / std::log(1000.0)) * (NP - 1)));
        const double cursorDb = juce::jmap((double)p.y, (double)getHeight(), 0.0, -48.0, 6.0);
        int best = -1; double bestD = 9.0;                         // within ~9 dB of a curve
        for (int t = 0; t < (int)traces.size(); ++t) {
            const auto& c = traces[(size_t)t].curve;
            if (i >= (int)c.size()) continue;
            const double d = std::abs(c[(size_t)i] - cursorDb);
            if (d < bestD) { bestD = d; best = t; }
        }
        return best;
    }
    float xOfFreq(double f) const {
        return (float)(std::log(juce::jlimit(20.0, 20000.0, f) / 20.0) / std::log(1000.0)) * (float)getWidth();
    }
    double freqAtX(float x) const {
        return 20.0 * std::pow(1000.0, (double)juce::jlimit(0.0f, 1.0f, x / (float)getWidth()));
    }
    std::vector<Trace> traces;
    std::vector<double> interference;
    std::vector<double> liveSpec;               // smoothed live-analyser curve (NP points)
    std::vector<LegendEntry> legend;            // colour → description rows (top-right); empty = hidden
    int legendActive = -1;
    std::vector<juce::Rectangle<int>> legendRects;   // row hit-boxes, rebuilt each paint
    static juce::Rectangle<int> gearRect() { return { 6, 4, 20, 20 }; }   // the view-options gear hotspot
    float mx = -1.0f, my = -1.0f;
    bool fltActive = false, fltHp = false, fltLp = false;          // a strip is active · its HPF / LPF are enabled
    double hpF = kHpMin, lpF = kLpMax; int hpDb_ = 24, lpDb_ = 12; // active strip's HPF/LPF cutoff + slope (dB/oct)
    juce::Colour lineColour { 0xffff8a3d };                        // active strip's colour (filter line/curve tint)
    int drag = 0;
    float dragStartY = 0.0f; int dragStartSlope = 2;              // line drag: Y anchor + slope index at grab
    static constexpr float kPxPerStep = 26.0f;                    // vertical pixels of drag per slope step
    float wheelAccum = 0.0f;                                       // wheel delta accumulated toward the next slope step
    static constexpr float kWheelStep = 0.3f;                      // notches of accumulated wheel per slope step
};
