// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
#include "UiSupport.h"

// Fender-TMP-style mic placement: click a dot = position (row) x distance (column) in
// one gesture. Speaker face on the left anchors the rows; distance columns in cm or in.
// Canvas-drawn + interactive; drives the position + distance fields. (Design polish later.)
class MicGrid : public juce::Component {
public:
    // One dot per grille mic. id = mic-row index; colour = the mic's slot colour.
    struct Dot { int id; juce::String pos; double dist; juce::Colour colour; bool active; };
    std::function<void(const juce::String&, double)> onSelect;        // empty-cell click -> move the ACTIVE mic
    std::function<void(int)> onDotSelect;                             // clicked a dot -> that mic becomes active
    std::function<void(int, const juce::String&, double)> onDotDrag;  // dragging a dot moves THAT mic
    void setUnit(const juce::String& u) { if (u != unit) { unit = u; repaint(); } }
    void setDots(std::vector<Dot> ds) { dots = std::move(ds); repaint(); }
    void mouseDown(const juce::MouseEvent& e) override {
        dragId = -1;
        const int hit = dotAt(e.position);
        if (hit >= 0) { dragId = dots[(size_t)hit].id; if (onDotSelect) onDotSelect(dragId); return; }
        if (e.position.x < speakerR() * 2.0f + 40.0f) return;         // ignore speaker/label clicks
        const auto [pos, dist] = cellAt(e.position);
        if (onSelect) onSelect(pos, dist);
    }
    void mouseDrag(const juce::MouseEvent& e) override {              // drag = direct manipulation, no mode
        if (dragId < 0 || e.position.x < speakerR() * 2.0f + 40.0f) return;
        const auto [pos, dist] = cellAt(e.position);
        if (onDotDrag) onDotDrag(dragId, pos, dist);
    }
    void mouseUp(const juce::MouseEvent&) override { dragId = -1; }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff16181d));
        const float R = speakerR(), cx = R + 6.0f, cy = (float)getHeight() * 0.5f;
        drawSpeaker(g, cx, cy, R);
        { const float capH = cy - R - 2.0f;                              // the empty strip above the speaker
          if (capH >= 12.0f) {
              g.setColour(juce::Colour(0xffeef0f6));
              g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
              g.drawText("Place the mics", 4, 1, juce::jmax(150, (int)(R * 2.0f) + 30), (int)capH,
                         juce::Justification::centredLeft);
          } }
        const auto& L = ladder();
        const float x0 = colX(0), x1 = colX((int)L.size() - 1);
        g.setFont(9.0f);
        for (int r = 0; r < NR; ++r) {                                   // rows aligned to the speaker radii
            const float y = rowY(r);
            g.setColour(juce::Colours::grey.withAlpha(0.30f)); g.drawHorizontalLine((int)y, cx, x0 - 6.0f);
            g.setColour(juce::Colours::grey);
            g.drawText(rows[r], (int)(cx + R + 2), (int)y - 7, (int)(x0 - (cx + R) - 20), 14, juce::Justification::centredRight);
        }
        g.setColour(juce::Colours::grey.withAlpha(0.55f));               // the cell lattice
        for (int r = 0; r < NR; ++r)
            for (int c = 0; c < (int)L.size(); ++c) g.fillEllipse(colX(c) - 2, rowY(r) - 2, 4, 4);
        for (const auto& d : dotXYs()) {                                 // placed mics (colour = mic)
            const auto& dot = dots[(size_t)d.idx];
            const float rr = dot.active ? 9.0f : 7.0f;
            g.setColour(dot.colour); g.fillEllipse(d.x - rr, d.y - rr, rr * 2, rr * 2);
            g.setColour(juce::Colour(0xff16181d)); g.fillEllipse(d.x - 2.5f, d.y - 2.5f, 5, 5);
            if (dot.active) { g.setColour(juce::Colours::white.withAlpha(0.85f)); g.drawEllipse(d.x - rr, d.y - rr, rr * 2, rr * 2, 1.4f); }
        }
        g.setColour(juce::Colour(0xff9778ff).withAlpha(0.5f));       // violet grid frame
        g.drawRect(x0 - 12.0f, rowY(0) - 12.0f, (x1 - x0) + 24.0f, (rowY(NR - 1) - rowY(0)) + 24.0f, 1.0f);
        g.setColour(juce::Colours::grey); g.setFont(9.0f);
        for (int c = 0; c < (int)L.size(); ++c) g.drawText(distLabel(L[(size_t)c]), (int)colX(c) - 18, (int)rowY(NR - 1) + 18, 36, 12, juce::Justification::centred);
    }
private:
    static constexpr int NR = 4;
    juce::String rows[NR] { "Cone Edge", "Center Cone", "Cap Edge", "Center Cap" };   // top -> bottom
    const float frac[NR] { 0.86f, 0.60f, 0.34f, 0.0f };                               // radial fraction (Cap = centre)
    juce::String unit = "cm";
    std::vector<Dot> dots;
    int dragId = -1;
    struct DotXY { int idx; float x, y; };
    struct RC { int r = -1, c = 0; };
    RC rcFor(const juce::String& pos, double dist) const {
        RC rc;
        for (int r = 0; r < NR; ++r) if (rows[r] == pos) rc.r = r;
        const auto& L = ladder(); double bd = 1e18;
        for (int c = 0; c < (int)L.size(); ++c) { const double dd = std::abs(L[c] - dist); if (dd < bd) { bd = dd; rc.c = c; } }
        return rc;
    }
    std::pair<juce::String, double> cellAt(juce::Point<float> p) const {
        const auto& L = ladder();
        int br = 0; float bd = 1e9f;
        for (int r = 0; r < NR; ++r) { const float d = std::abs(p.y - rowY(r)); if (d < bd) { bd = d; br = r; } }
        int bc = 0; float bx = 1e9f;
        for (int c = 0; c < (int)L.size(); ++c) { const float d = std::abs(p.x - colX(c)); if (d < bx) { bx = d; bc = c; } }
        return { rows[br], L[(size_t)bc] };
    }
    // Screen positions with a micro-offset for stacked dots (Fredman: two mics in one cell).
    std::vector<DotXY> dotXYs() const {
        std::vector<DotXY> out;
        std::vector<RC> rcs(dots.size());
        for (size_t i = 0; i < dots.size(); ++i) rcs[i] = rcFor(dots[i].pos, dots[i].dist);
        for (size_t i = 0; i < dots.size(); ++i) {
            if (rcs[i].r < 0) continue;
            int stack = 0, before = 0;
            for (size_t j = 0; j < dots.size(); ++j)
                if (rcs[j].r >= 0 && rcs[j].r == rcs[i].r && rcs[j].c == rcs[i].c) { ++stack; if (j < i) ++before; }
            const float off = (stack > 1) ? ((float)before - (float)(stack - 1) * 0.5f) * 9.0f : 0.0f;
            out.push_back({ (int)i, colX(rcs[i].c) + off, rowY(rcs[i].r) });
        }
        return out;
    }
    // Hit-test dots; on a stack, clicking cycles to the next mic after the active one.
    int dotAt(juce::Point<float> p) const {
        std::vector<int> hits;
        for (const auto& d : dotXYs())
            if (p.getDistanceFrom({ d.x, d.y }) <= 10.0f) hits.push_back(d.idx);
        if (hits.empty()) return -1;
        if (hits.size() == 1) return hits[0];
        for (size_t k = 0; k < hits.size(); ++k)
            if (dots[(size_t)hits[k]].active) return hits[(k + 1) % hits.size()];
        return hits[0];
    }
    float speakerR() const { return (float)getHeight() * 0.40f; }
    float rowY(int r) const { return (float)getHeight() * 0.5f - frac[r] * speakerR(); }
    float colX(int c) const { const auto& L = ladder(); const float x0 = speakerR() * 2.0f + 96.0f, x1 = (float)getWidth() - 24.0f; return x0 + (float)c / (float)(L.size() - 1) * (x1 - x0); }
    bool fancy = true;   // realistic driver (drawn by Fable 5); set false for the flat fallback
    void drawSpeaker(juce::Graphics& g, float cx, float cy, float R) const {
        if (!fancy) {
            g.setColour(juce::Colour(0xff2b3038)); g.fillEllipse(cx - R, cy - R, 2 * R, 2 * R);
            g.setColour(juce::Colour(0xff353c46)); g.fillEllipse(cx - R * 0.86f, cy - R * 0.86f, 2 * R * 0.86f, 2 * R * 0.86f);
            g.setColour(juce::Colour(0xff424b58)); g.fillEllipse(cx - R * 0.34f, cy - R * 0.34f, 2 * R * 0.34f, 2 * R * 0.34f);
            g.setColour(juce::Colours::grey.withAlpha(0.5f)); g.drawEllipse(cx - R, cy - R, 2 * R, 2 * R, 1.0f);
            return;
        }
        const float pi = 3.14159265f;

        // ---- Radial zone fractions (grid contract — do NOT foreshorten) ----
        const float rDust    = 0.34f * R;   // domed dust cap
        const float rConeOut = 0.86f * R;   // cone outer edge
        const float rSurrOut = 0.97f * R;   // ribbed surround outer edge
        const float rRim     = 1.00f * R;   // cast basket rim

        // Top-left light bias reused across gradients / shading
        const float lox = cx - 0.34f * R;
        const float loy = cy - 0.34f * R;
        const float lightA = -2.356f;       // ~ -135deg  (light coming from top-left)

        auto disc = [&g](float dx, float dy, float rr) { g.fillEllipse(dx - rr, dy - rr, rr * 2.0f, rr * 2.0f); };
        auto ring = [&g](float dx, float dy, float rr, float t) { g.drawEllipse(dx - rr, dy - rr, rr * 2.0f, rr * 2.0f, t); };

        // ================= 1. Cast-metal basket rim =================
        {
            juce::ColourGradient gr(juce::Colour(0xff7c808a), lox, loy,
                                    juce::Colour(0xff202127), cx + 0.72f * R, cy + 0.82f * R, true);
            gr.addColour(0.55, juce::Colour(0xff494c53));
            g.setGradientFill(gr);
            disc(cx, cy, rRim);
        }
        g.setColour(juce::Colour(0x33ffffff)); ring(cx, cy, rRim - 0.8f, 1.3f);
        g.setColour(juce::Colour(0x55000000)); ring(cx, cy, rSurrOut + 0.6f, 1.4f);

        // ================= 2. Ribbed surround (raised half-roll) =================
        {
            juce::ColourGradient sg(juce::Colour(0xff17181c), cx, cy,
                                    juce::Colour(0xff232429), cx + rSurrOut, cy, true);
            sg.addColour(0.886, juce::Colour(0xff2b2c32));   // inner valley
            sg.addColour(0.945, juce::Colour(0xff595c64));   // lit crown of the roll
            sg.addColour(1.000, juce::Colour(0xff26272c));   // outer valley
            g.setGradientFill(sg);
            disc(cx, cy, rSurrOut);
        }

        // ================= 3. Cone (black pressed paper, deep toward center) =================
        {
            juce::ColourGradient cg(juce::Colour(0xff101116), cx, cy,
                                    juce::Colour(0xff3b3c43), cx + rConeOut, cy, true);
            cg.addColour(0.395, juce::Colour(0xff141519));   // meets dust cap
            cg.addColour(0.82,  juce::Colour(0xff2a2b31));
            g.setGradientFill(cg);
            disc(cx, cy, rConeOut);
        }
        {
            juce::ColourGradient sh(juce::Colour(0x2effffff), cx - 0.62f * rConeOut, cy - 0.62f * rConeOut,
                                    juce::Colour(0x00ffffff), cx + 0.25f * rConeOut, cy + 0.55f * rConeOut, false);
            g.setGradientFill(sh);
            disc(cx, cy, rConeOut);
        }
        g.setColour(juce::Colour(0x16000000)); ring(cx, cy, 0.58f * R, 1.0f);
        g.setColour(juce::Colour(0x12ffffff)); ring(cx, cy, 0.585f * R, 1.0f);
        g.setColour(juce::Colour(0x16000000)); ring(cx, cy, 0.72f * R, 1.0f);
        g.setColour(juce::Colour(0x66000000)); ring(cx, cy, rConeOut, 1.4f);

        // ================= 4. Surround pleats (radial ribs, light-modulated) =================
        {
            const int nRibs = 56;
            const float ri = 0.868f * R, ro = 0.963f * R;
            for (int i = 0; i < nRibs; ++i) {
                const float a = (float)i / (float)nRibs * 2.0f * pi;
                const float ca = std::cos(a), sa = std::sin(a);
                const float f  = 0.5f + 0.5f * std::cos(a - lightA);
                g.setColour(juce::Colours::white.withAlpha(0.04f + 0.20f * f));
                g.drawLine(cx + ca * ri, cy + sa * ri, cx + ca * ro, cy + sa * ro, 1.0f);
                const float a2 = a + pi / (float)nRibs;
                const float c2 = std::cos(a2), s2 = std::sin(a2);
                g.setColour(juce::Colours::black.withAlpha(0.10f + 0.16f * f));
                g.drawLine(cx + c2 * ri, cy + s2 * ri, cx + c2 * ro, cy + s2 * ro, 1.0f);
            }
        }

        // ================= 5. Domed dust cap =================
        {
            juce::ColourGradient dg(juce::Colour(0xff787b84), cx - 0.12f * R, cy - 0.12f * R,
                                    juce::Colour(0xff090a0d), cx - 0.12f * R + 0.44f * R, cy - 0.12f * R, true);
            dg.addColour(0.35, juce::Colour(0xff3a3c42));
            dg.addColour(0.72, juce::Colour(0xff17181c));
            g.setGradientFill(dg);
            disc(cx, cy, rDust);
        }
        g.setColour(juce::Colour(0x77000000)); ring(cx, cy, rDust, 1.6f);
        g.setColour(juce::Colour(0x2affffff)); ring(cx, cy, rDust - 1.4f, 1.0f);
        {
            const float sx = cx - 0.12f * R, sy = cy - 0.12f * R;
            juce::ColourGradient sp(juce::Colour(0xb0ffffff), sx, sy,
                                    juce::Colour(0x00ffffff), sx + 0.12f * R, sy, true);
            g.setGradientFill(sp);
            disc(sx, sy, 0.12f * R);
        }

        // ================= 6. Mounting-screw lugs on the rim =================
        {
            const float lr = 0.985f * R, padR = 0.052f * R;
            for (int k = 0; k < 4; ++k) {
                const float a  = pi * 0.25f + (float)k * pi * 0.5f;   // 45,135,225,315
                const float px = cx + std::cos(a) * lr;
                const float py = cy + std::sin(a) * lr;
                g.setColour(juce::Colour(0x66000000)); disc(px + 0.6f, py + 1.0f, padR * 1.06f);
                juce::ColourGradient pg(juce::Colour(0xff8d919a), px - padR * 0.4f, py - padR * 0.4f,
                                        juce::Colour(0xff2f3137), px + padR, py + padR, true);
                g.setGradientFill(pg);
                disc(px, py, padR);
                g.setColour(juce::Colour(0xff14151a)); disc(px, py, padR * 0.46f);
                g.setColour(juce::Colour(0x66ffffff)); g.drawLine(px - padR * 0.4f, py - padR * 0.15f, px + padR * 0.4f, py - padR * 0.15f, 1.0f);
            }
        }

        g.setColour(juce::Colour(0xff0b0c0f)); ring(cx, cy, rRim, 1.4f);
    }
    const std::vector<double>& ladder() const {
        static const std::vector<double> in { 0, .5, 1, 2, 3, 4, 5, 6 };
        static const std::vector<double> cm { 0, 1, 2, 3, 5, 8, 10, 15 };
        return unit == "in" ? in : cm;
    }
    juce::String distLabel(double v) const {
        return unit == "in" ? (juce::String(v, (v == (double)(int)v) ? 0 : 1) + "\"") : juce::String((int)v);
    }
};
