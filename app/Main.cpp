// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// OrbitCapture — the JUCE application shell: MainWindow + JUCEApplication. Everything else
// (the header, tabs, capture/review/export wiring) lives in src/CaptureComponent.h.
#include "CaptureComponent.h"

class MainWindow : public juce::DocumentWindow {
public:
    MainWindow() : juce::DocumentWindow("OrbitCapture", juce::Colours::darkgrey, juce::DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        setContentOwned(new CaptureComponent(), true);          // header + tabs fill the window
        centreWithSize(760, 860);
        setResizable(true, true);
        setResizeLimits(680, 700, 4000, 3000);                  // wide enough for the header; grows with mic rows
        setVisible(true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class OrbitCaptureApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override    { return "OrbitCapture"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String&) override       { window.reset(new MainWindow()); }
    void shutdown() override                            { window = nullptr; }
private:
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION(OrbitCaptureApplication)
