// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
#include "UiSupport.h"
#include "BinaryData.h"        // embedded brand assets (catlogo.svg)

// Clickable header strip: [cat] [orbit mark]  OrbitCapture  by Darwin's Cat — the branded run
// (cat, mark, wordmark, byline) links to darwinscat.com (utm-tagged); everything right of the
// byline's end is NOT the link (version readout lives there). Michroma wordmark; the byline is an
// inline violet continuation on the same baseline. Soft accent halo on hover, hand cursor only
// over the link run.
struct BrandHeader : juce::Component {
    std::unique_ptr<juce::Drawable> logo {
        juce::Drawable::createFromImageData(BinaryData::catlogo_svg, (size_t)BinaryData::catlogo_svgSize) };
    juce::Typeface::Ptr michroma {
        juce::Typeface::createSystemTypefaceFor(BinaryData::MichromaRegular_ttf, (size_t)BinaryData::MichromaRegular_ttfSize) };
    bool hover = false;
    int clickRight = 1 << 30;                  // set from resized(): the end of the byline text
    juce::String version;                      // dim right-aligned readout (the app sets it once)
    bool linkArea(juce::Point<float> p) const { return p.x < (float)clickRight; }
    void updateHover(juce::Point<float> p) {
        const bool h = linkArea(p);
        setMouseCursor(h ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        if (h != hover) { hover = h; repaint(); }
    }
    void mouseEnter(const juce::MouseEvent& e) override { updateHover(e.position); }
    void mouseMove (const juce::MouseEvent& e) override { updateHover(e.position); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp   (const juce::MouseEvent& e) override {
        if (getLocalBounds().contains(e.getPosition()) && linkArea(e.position))
            juce::URL("https://darwinscat.com/?utm_source=orbitcapture&utm_medium=app").launchInDefaultBrowser();
    }
    static float textWidth(const juce::Font& f, const juce::String& s) {
        juce::GlyphArrangement ga; ga.addLineOfText(f, s, 0.0f, 0.0f);
        return ga.getBoundingBox(0, -1, true).getWidth();
    }
    // Where the branded run ends — the link hit-area boundary. Mirrors paint()'s x math.
    float contentRight() const {
        const float h = (float)getHeight(), d = h * 0.86f;
        const auto wf = juce::Font(juce::FontOptions().withHeight(h * 0.44f).withTypeface(michroma));
        const auto bf = juce::Font(juce::FontOptions().withHeight(h * 0.26f).withTypeface(michroma));
        return h + 6.0f + d + 14.0f + textWidth(wf, "OrbitCapture") + 14.0f + textWidth(bf, "by Darwin's Cat") + 8.0f;
    }
    void paint(juce::Graphics& g) override {
        const float h = (float)getHeight();
        const float cy = getLocalBounds().toFloat().getCentreY();
        if (hover) { g.setColour(brand::violet.withAlpha(0.14f));   // halo over the LINK run only
                     g.fillRoundedRectangle(getLocalBounds().toFloat().withRight((float)clickRight), 8.0f); }
        if (logo != nullptr)                                          // cat logo (square) far left
            logo->drawWithin(g, juce::Rectangle<float>(4.0f, 2.0f, h - 4.0f, h - 4.0f), juce::RectanglePlacement::centred, 1.0f);
        const float d = h * 0.86f;                                    // orbit mark (bigger)
        brand::drawOrbit(g, h + 6.0f + d * 0.5f, cy, d, hover);
        float x = h + 6.0f + d + 14.0f;
        const auto wf = juce::Font(juce::FontOptions().withHeight(h * 0.44f).withTypeface(michroma));   // "OrbitCapture" (big)
        const float baseline = cy + (wf.getAscent() - wf.getDescent()) * 0.5f;
        g.setFont(wf);
        g.setColour(hover ? juce::Colours::white : juce::Colour(0xffeef0f6));
        g.drawSingleLineText("OrbitCapture", juce::roundToInt(x), juce::roundToInt(baseline));
        x += textWidth(wf, "OrbitCapture") + 14.0f;
        const auto bf = juce::Font(juce::FontOptions().withHeight(h * 0.26f).withTypeface(michroma));   // inline byline (violet)
        g.setFont(bf);
        g.setColour(hover ? brand::violet.brighter(0.3f) : brand::violet);
        g.drawSingleLineText("by Darwin's Cat", juce::roundToInt(x), juce::roundToInt(baseline));
        if (version.isNotEmpty()) {                                   // dim version, far right, same line
            g.setFont(juce::Font(juce::FontOptions().withHeight(h * 0.24f)));
            g.setColour(juce::Colours::white.withAlpha(0.35f));
            g.drawText(version, getLocalBounds().reduced(12, 0), juce::Justification::centredRight, false);
        }
        g.setColour(juce::Colour(0x22ffffff)); g.fillRect(0, getHeight() - 1, getWidth(), 1);   // separator
    }
};
