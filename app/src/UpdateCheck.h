// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
// OrbitCapture's update check — the family's shared checker (felitronics-appkit: opt-in,
// user-click only, silent failure, owned-thread join on destruction) plus this app's
// PropertiesFile owner. The app had no key-value store before this (sessions/gear lists are
// plain JSON next to it in the same app-data dir); the badge is the first need for one.
//
// COMPOSITION, not the family's usual adapter-inheritance: the checker's ctor already reads the
// store (badge-clear on version catch-up), so `props` must be fully constructed first — member
// order guarantees exactly that, which a base-class Config lambda pointing into the derived
// object could not.
#include <felitronics/appkit/UpdateChecker.h>

namespace ocap {

class UpdateCheck {
public:
    using Result = felitronics::appkit::UpdateChecker::Result;

    explicit UpdateCheck (juce::String currentVersion)
        : props (settingsFile(), {}),
          checker ({ .ownerRepo      = "darwinscat/orbit-capture",
                     .productName    = "OrbitCapture",
                     .currentVersion = std::move (currentVersion),
                     .settings       = [this] { return &props; } }) {}

    void checkNow (std::function<void (Result)> cb) { checker.checkNow (std::move (cb)); }
    bool updateAvailable() const                    { return checker.updateAvailable(); }
    juce::String storedLatest() const               { return checker.storedLatest(); }
    juce::String currentVersion() const             { return checker.currentVersion(); }
    juce::String releasesPageUrl() const            { return checker.releasesPageUrl(); }

private:
    static juce::File settingsFile() {
        // ~/Library/Application Support/OrbitCapture/settings.xml — the same app-data folder as
        // lists.json and sessions/ (see FileListStore / SessionStore).
        auto f = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                     .getChildFile ("OrbitCapture").getChildFile ("settings.xml");
        f.getParentDirectory().createDirectory();
        return f;
    }

    juce::PropertiesFile props;                     // BEFORE checker — its ctor reads the store
    felitronics::appkit::UpdateChecker checker;
};

} // namespace ocap
