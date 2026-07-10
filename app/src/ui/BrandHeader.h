// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
#include "UiSupport.h"
#include "BinaryData.h"        // embedded brand assets (catlogo.svg)

// Clickable header strip: [cat] [orbit mark]  OrbitCapture  by Darwin's Cat — the WHOLE strip
// (cat, mark, wordmark) links to darwinscat.com (utm-tagged). Michroma wordmark; the byline is an
// inline violet continuation on the same baseline. Soft accent halo on hover.
struct BrandHeader : juce::Component {
    std::unique_ptr<juce::Drawable> logo {
        juce::Drawable::createFromImageData(BinaryData::catlogo_svg, (size_t)BinaryData::catlogo_svgSize) };
    juce::Typeface::Ptr michroma {
        juce::Typeface::createSystemTypefaceFor(BinaryData::MichromaRegular_ttf, (size_t)BinaryData::MichromaRegular_ttfSize) };
    bool hover = false;
    int clickRight = 1 << 30;                  // clicks right of this x belong to the overlaid session controls
    BrandHeader() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    bool linkArea(juce::Point<float> p) const { return p.x < (float)clickRight; }
    void mouseEnter(const juce::MouseEvent& e) override { hover = linkArea(e.position); repaint(); }
    void mouseMove (const juce::MouseEvent& e) override {
        const bool h = linkArea(e.position);
        if (h != hover) { hover = h; repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp   (const juce::MouseEvent& e) override {
        if (getLocalBounds().contains(e.getPosition()) && linkArea(e.position))
            juce::URL("https://darwinscat.com/?utm_source=orbitcapture&utm_medium=app").launchInDefaultBrowser();
    }
    static float textWidth(const juce::Font& f, const juce::String& s) {
        juce::GlyphArrangement ga; ga.addLineOfText(f, s, 0.0f, 0.0f);
        return ga.getBoundingBox(0, -1, true).getWidth();
    }
    void paint(juce::Graphics& g) override {
        const float h = (float)getHeight();
        const float cy = getLocalBounds().toFloat().getCentreY();
        if (hover) { g.setColour(brand::violet.withAlpha(0.14f)); g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f); }
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
        g.setColour(juce::Colour(0x22ffffff)); g.fillRect(0, getHeight() - 1, getWidth(), 1);   // separator
    }
};
