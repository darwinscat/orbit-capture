// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
#include "UiSupport.h"
#include <span>
#include <felitronics/analysis/offline/SpectrumCurve.h>   // spectrum display curves (core)

// Magnitude spectrum of the last captured IR (log-f 20..20k, dB, 1/12-oct smoothed) — the cabinet's
// EQ curve at a glance, grown into the blend overlay (per-mic curves + blend + interference tint).
//
// The interactive HPF/LPF (draggable lines + slope badges + stopband shading + the filter's own
// response curve) live in a TRANSPARENT OVERLAY child on top. Dragging a filter repaints only that
// thin overlay — the heavy mic/blend curves underneath are NOT re-stroked per mouse-move — so the
// drag stays smooth while the full picture keeps redrawing normally on release. The overlay's
// hitTest is true only near an enabled line, so clicks elsewhere (legend, gear, trace-pick) fall
// through to the base view beneath.
class SpectrumView : public juce::Component {
public:
    static constexpr int NP = 256;
    struct Trace { std::vector<double> curve; juce::Colour colour; float thick; bool fill; };

    static constexpr int kSlopes[7] = { 6, 12, 24, 36, 48, 72, 96 };   // discrete dB/oct steps
    static int slopeIdxOfDb(int db) { for (int i = 0; i < 7; ++i) if (kSlopes[i] == db) return i; return 2; }
    static constexpr double kHpMin = 20.0, kHpMax = 2000.0, kLpMin = 500.0, kLpMax = 20000.0;

    std::function<void(double, int, double, int)> onFilterDrag; // live → (hpHz, hpSlopeDb, lpHz, lpSlopeDb)
    std::function<void()> onFilterDragEnd;                      // release / wheel → persist
    std::function<void(int)> onPickTrace;                       // click a curve → select its strip (composite = Master)
    std::function<void(int)> onPickLegend;                      // click a legend row → its entry index
    std::function<void(juce::Rectangle<int>)> onGearMenu;       // click the gear → open the view menu (screen coords)

    struct LegendEntry { juce::String text; juce::Colour colour; bool dim = false; };

    SpectrumView() {
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        addAndMakeVisible(filter);                              // the interactive filter layer, on top
    }
    void resized() override { filter.setBounds(getLocalBounds()); }

    void setLegend(std::vector<LegendEntry> e, int active) { legend = std::move(e); legendActive = active; repaint(); }
    void setActiveFilter(bool active, bool hpOn, bool lpOn, double hpHz, int hpDb, double lpHz, int lpDb, juce::Colour col) {
        filter.set(active, hpOn, lpOn, hpHz, hpDb, lpHz, lpDb, col);   // repaints only the overlay (cheap)
    }
    static juce::String freqLabel(double f) {
        return f >= 1000.0 ? juce::String(f / 1000.0, f >= 10000.0 ? 1 : 2) + "k" : juce::String((int)std::lround(f));
    }
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
    void setTraces(std::vector<Trace> t, std::vector<double> interf) {
        traces = std::move(t); interference = std::move(interf); repaint();
    }
    void setLiveSpectrum(const std::vector<double>& c) {
        if (c.empty()) { if (!liveSpec.empty()) { liveSpec.clear(); repaint(); } return; }
        if (liveSpec.size() != c.size()) liveSpec.assign(c.size(), -120.0);
        for (size_t i = 0; i < c.size(); ++i) liveSpec[i] += (c[i] - liveSpec[i]) * 0.4;   // temporal smoothing
        repaint();
    }

    // Base view: crosshair + legend + gear hover follow the pointer over the empty graph area
    // (near a filter line the overlay takes the events; that's fine).
    void mouseMove(const juce::MouseEvent& e) override { mx = (float)e.position.x; my = (float)e.position.y; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { mx = -1.0f; repaint(); }
    void mouseDown(const juce::MouseEvent& e) override {
        if (gearRect().contains(e.position.toInt())) {
            if (onGearMenu) onGearMenu(localAreaToGlobal(gearRect())); return; }
        for (int i = 0; i < (int)legendRects.size(); ++i)
            if (legendRects[(size_t)i].contains(e.position.toInt())) { if (onPickLegend) onPickLegend(i); return; }
        if (onPickTrace) { const int t = nearestTrace(e.position); if (t >= 0) onPickTrace(t); }
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
        {                                                              // the view-options gear (top-left)
            const bool hover = mx >= 0.0f && gearRect().contains(juce::Point<int>((int)mx, (int)my));
            g.setColour(juce::Colours::white.withAlpha(hover ? 0.95f : 0.45f));
            g.setFont(juce::FontOptions(34.0f));
            g.drawText(juce::String::fromUTF8("\xe2\x9a\x99"), gearRect(), juce::Justification::centred);
        }
        legendRects.clear();
        legendBottomY = b.getY() + 4.0f;
        if (!legend.empty()) {                                         // colour → mic legend, flowing across the top
            const juce::Font lf(juce::FontOptions(10.0f));
            const int rowH = 15, chip = 8, gap = 4, padX = 6;
            const float left = b.getX() + 50.0f, right = b.getRight() - 6.0f;   // clear the (bigger) gear
            float x = left, y = b.getY() + 5.0f;
            g.setFont(lf);
            for (int i = 0; i < (int)legend.size(); ++i) {
                const auto& e = legend[(size_t)i];
                const int tw = (int)std::ceil(juce::GlyphArrangement::getStringWidth(lf, e.text));
                const float w = (float)(chip + gap + tw + 2 * padX);
                if (x + w > right && x > left) { x = left; y += rowH + 3.0f; }
                juce::Rectangle<int> cell((int)x, (int)y, (int)w, rowH);
                legendRects.push_back(cell);
                const bool act = i == legendActive, dim = e.dim;
                g.setColour(juce::Colours::black.withAlpha(0.42f));
                g.fillRoundedRectangle(cell.toFloat(), 4.0f);
                g.setColour(e.colour.withAlpha(dim ? 0.3f : act ? 1.0f : 0.85f));
                g.fillRoundedRectangle(x + padX, y + rowH / 2.0f - chip / 2.0f, (float)chip, (float)chip, 2.0f);
                if (act) g.drawRoundedRectangle(x + padX - 1.5f, y + rowH / 2.0f - chip / 2.0f - 1.5f, chip + 3.0f, chip + 3.0f, 3.0f, 1.0f);
                g.setColour(juce::Colours::white.withAlpha(dim ? 0.35f : act ? 1.0f : 0.78f));
                g.setFont(juce::FontOptions(10.0f, act ? juce::Font::bold : juce::Font::plain));
                g.drawText(e.text, (int)(x + padX + chip + gap), (int)y, tw + 4, rowH, juce::Justification::centredLeft);
                g.setFont(lf);
                x += w + 4.0f;
                legendBottomY = (float)(y + rowH);                    // readout sits below this
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
            g.fillEllipse(mx - 3.0f, y - 3.0f, 6.0f, 6.0f);
            const juce::String txt = (f >= 1000.0 ? juce::String(f / 1000.0, 2) + " kHz" : juce::String((int)std::lround(f)) + " Hz")
                                   + "   " + juce::String(db, 1) + " dB";
            const int tw = 120;
            const bool flip = mx > b.getRight() - (float)tw - 10.0f;
            g.setFont(11.0f); g.setColour(juce::Colours::white);
            g.drawText(txt, (int)mx + (flip ? -tw - 6 : 6), (int)legendBottomY + 3, tw, 14,   // below the legend
                       flip ? juce::Justification::right : juce::Justification::left);
        }
    }

private:
    // ---- the interactive filter layer (thin, cheap to repaint on drag) ----------------------------
    struct FilterOverlay : juce::Component {
        SpectrumView& owner;
        bool active = false, hp = false, lp = false;
        double hpF = kHpMin, lpF = kLpMax; int hpDb = 24, lpDb = 12;
        juce::Colour colour { 0xffff8a3d };
        int drag = 0; float dragStartY = 0.0f; int dragStartSlope = 2;
        float wheelAccum = 0.0f; float mx = -1.0f;
        static constexpr float kPxPerStep = 26.0f, kWheelStep = 0.3f;

        explicit FilterOverlay(SpectrumView& o) : owner(o) {}
        void set(bool a, bool hpOn, bool lpOn, double hz, int hdb, double lhz, int ldb, juce::Colour c) {
            active = a; hp = hpOn; lp = lpOn; hpF = hz; hpDb = hdb; lpF = lhz; lpDb = ldb; colour = c; repaint();
        }
        float xOfFreq(double f) const {
            return (float)(std::log(juce::jlimit(20.0, 20000.0, f) / 20.0) / std::log(1000.0)) * (float)getWidth();
        }
        double freqAtX(float x) const {
            return 20.0 * std::pow(1000.0, (double)juce::jlimit(0.0f, 1.0f, x / (float)getWidth()));
        }
        int lineHit(float x) const {                             // 1 = hp · 2 = lp · 0 = none (enabled lines only)
            if (!active) return 0;
            const float dh = hp ? std::abs(x - xOfFreq(hpF)) : 1.0e9f;
            const float dl = lp ? std::abs(x - xOfFreq(lpF)) : 1.0e9f;
            if (dh <= 10.0f && dh <= dl) return 1;
            if (dl <= 10.0f)             return 2;
            return 0;
        }
        // Only intercept the mouse near an enabled line — everything else falls through to the base.
        bool hitTest(int x, int) override { return lineHit((float)x) != 0; }

        void mouseMove(const juce::MouseEvent& e) override {
            mx = (float)e.position.x;
            setMouseCursor(lineHit(mx) ? juce::MouseCursor::UpDownLeftRightResizeCursor
                                       : juce::MouseCursor::CrosshairCursor);
            repaint();
        }
        void mouseExit(const juce::MouseEvent&) override { mx = -1.0f; repaint(); }
        void mouseDown(const juce::MouseEvent& e) override {
            drag = lineHit((float)e.position.x);
            if (drag != 0) { dragStartY = (float)e.position.y; dragStartSlope = slopeIdxOfDb(drag == 1 ? hpDb : lpDb); }
        }
        void mouseDrag(const juce::MouseEvent& e) override {     // X = cutoff · Y (up = shallower) = slope
            if (drag == 0) return;
            if (drag == 1) hpF = juce::jlimit(kHpMin, std::min(kHpMax, lpF * 0.5), freqAtX((float)e.position.x));
            else           lpF = juce::jlimit(std::max(kLpMin, hpF * 2.0), kLpMax, freqAtX((float)e.position.x));
            const int idx = juce::jlimit(0, 6, dragStartSlope + (int)std::lround(((float)e.position.y - dragStartY) / kPxPerStep));
            (drag == 1 ? hpDb : lpDb) = kSlopes[idx];
            if (owner.onFilterDrag) owner.onFilterDrag(hpF, hpDb, lpF, lpDb);
        }
        void mouseUp(const juce::MouseEvent&) override { if (drag) { drag = 0; if (owner.onFilterDragEnd) owner.onFilterDragEnd(); } }
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
            if (!active || (!hp && !lp) || w.deltaY == 0.0f) return;
            wheelAccum += (w.isReversed ? -w.deltaY : w.deltaY);
            if (std::abs(wheelAccum) < kWheelStep) return;
            const int dir = wheelAccum > 0.0f ? 1 : -1;
            wheelAccum -= (float)dir * kWheelStep;
            const float x = (float)e.position.x;
            const bool onHp = (hp && lp) ? (std::abs(x - xOfFreq(hpF)) <= std::abs(x - xOfFreq(lpF))) : hp;
            int& db = onHp ? hpDb : lpDb;
            db = kSlopes[juce::jlimit(0, 6, slopeIdxOfDb(db) + dir)];
            if (owner.onFilterDrag) owner.onFilterDrag(hpF, hpDb, lpF, lpDb);
            if (owner.onFilterDragEnd) owner.onFilterDragEnd();
            repaint();
        }
        void mouseDoubleClick(const juce::MouseEvent& e) override {
            const int h = lineHit((float)e.position.x);
            if (h == 0) return;
            if (h == 1) { hpF = 80.0; hpDb = 24; } else { lpF = 8000.0; lpDb = 12; }
            if (owner.onFilterDrag) owner.onFilterDrag(hpF, hpDb, lpF, lpDb);
            if (owner.onFilterDragEnd) owner.onFilterDragEnd();
            repaint();
        }
        void paint(juce::Graphics& g) override {
            if (!active || (!hp && !lp)) return;
            auto b = getLocalBounds().toFloat();
            auto yOf = [&](double db) { return juce::jmap((float)db, -48.0f, 6.0f, b.getBottom(), b.getY()); };
            const float xh = xOfFreq(hpF), xl = xOfFreq(lpF);
            g.setColour(juce::Colours::black.withAlpha(0.28f));        // shade the enabled stopband(s)
            if (hp) g.fillRect(juce::Rectangle<float>(b.getX(), b.getY(), xh - b.getX(), b.getHeight()));
            if (lp) g.fillRect(juce::Rectangle<float>(xl, b.getY(), b.getRight() - xl, b.getHeight()));
            {                                                         // the filter's own EQ response curve
                juce::Path eq; const int NE = 240;
                for (int i = 0; i <= NE; ++i) {
                    const double f = 20.0 * std::pow(1000.0, (double)i / NE);
                    const double nh = hpDb / 3.0, nl = lpDb / 3.0;    // Butterworth order n = slope/6
                    double mag2 = 1.0;
                    if (hp) mag2 *= 1.0 / (1.0 + std::pow(hpF / f, nh));
                    if (lp) mag2 *= 1.0 / (1.0 + std::pow(f / lpF, nl));
                    const double db = 10.0 * std::log10(mag2 + 1e-12);
                    const float x = b.getX() + (float)i / (float)NE * b.getWidth();
                    const float y = yOf(juce::jmax(-200.0, db > 6.0 ? 6.0 : db));   // below the floor it exits (clipped)
                    if (i == 0) eq.startNewSubPath(x, y); else eq.lineTo(x, y);
                }
                g.setColour(colour.withAlpha(0.85f));
                g.strokePath(eq, juce::PathStrokeType(1.6f));
            }
            auto line = [&](float x, const char* tag, double hz, int db, bool leftLabel) {
                const bool hot = lineHit(mx) == (tag[0] == 'H' ? 1 : 2);
                g.setColour(colour.withAlpha(hot ? 1.0f : 0.85f));
                g.drawVerticalLine((int)x, b.getY(), b.getBottom());
                // slope BADGE at the line centre: the number is the dB/oct, the ▲/▼ chevrons hint
                // "drag up/down to change the slope" (left/right moves the cutoff).
                const float bw = 26.0f, bh = 40.0f, cyc = b.getCentreY();
                juce::Rectangle<float> badge(x - bw / 2.0f, cyc - bh / 2.0f, bw, bh);
                g.setColour(colour.withAlpha(hot ? 1.0f : 0.9f));
                g.fillRoundedRectangle(badge, 5.0f);
                g.setColour(juce::Colours::black.withAlpha(0.9f));
                auto chev = [&](float cyv, bool upward) {
                    juce::Path t; const float s = 3.2f;
                    if (upward) t.addTriangle(x - s, cyv + s * 0.7f, x + s, cyv + s * 0.7f, x, cyv - s * 0.7f);
                    else        t.addTriangle(x - s, cyv - s * 0.7f, x + s, cyv - s * 0.7f, x, cyv + s * 0.7f);
                    g.fillPath(t);
                };
                chev(badge.getY() + 6.0f, true);
                chev(badge.getBottom() - 6.0f, false);
                g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
                g.drawText(juce::String(db), badge, juce::Justification::centred);
                g.setColour(colour);
                g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
                g.drawText(juce::String(tag) + " " + freqLabel(hz) + " Hz \xc2\xb7 " + juce::String(db) + " dB/oct",
                           (int)(leftLabel ? x - 116 : x + 6), (int)b.getBottom() - 30, 110, 12,
                           leftLabel ? juce::Justification::right : juce::Justification::left);
            };
            if (hp) line(xh, "HPF", hpF, hpDb, false);
            if (lp) line(xl, "LPF", lpF, lpDb, true);
        }
    };

    int nearestTrace(juce::Point<float> p) const {                 // the mic curve closest to the click (dB distance)
        if (traces.empty()) return -1;
        const int i = juce::jlimit(0, NP - 1, (int)std::lround((std::log(freqAtX(p.x) / 20.0) / std::log(1000.0)) * (NP - 1)));
        const double cursorDb = juce::jmap((double)p.y, (double)getHeight(), 0.0, -48.0, 6.0);
        int best = -1; double bestD = 9.0;
        for (int t = 0; t < (int)traces.size(); ++t) {
            const auto& c = traces[(size_t)t].curve;
            if (i >= (int)c.size()) continue;
            const double d = std::abs(c[(size_t)i] - cursorDb);
            if (d < bestD) { bestD = d; best = t; }
        }
        return best;
    }
    double freqAtX(float x) const { return 20.0 * std::pow(1000.0, (double)juce::jlimit(0.0f, 1.0f, x / (float)getWidth())); }
    static juce::Rectangle<int> gearRect() { return { 5, 3, 40, 40 }; }   // the view-options gear hotspot

    std::vector<Trace> traces;
    std::vector<double> interference;
    std::vector<double> liveSpec;               // smoothed live-analyser curve (NP points)
    std::vector<LegendEntry> legend;            // colour → description rows (top); empty = hidden
    int legendActive = -1;
    std::vector<juce::Rectangle<int>> legendRects;   // row hit-boxes, rebuilt each paint
    float legendBottomY = 0.0f;                       // y under the legend — the cursor readout sits here
    float mx = -1.0f, my = -1.0f;
    FilterOverlay filter { *this };
};
