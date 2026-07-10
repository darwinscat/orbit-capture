// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
// OrbitCapture — session bundle assembly (juce_core File/Zip I/O). De-monolith step 8.
//
// The zip mechanics of Export's "session bundle", extracted verbatim from CaptureComponent's
// exportBundle: every file under the session dir (raw takes + reports) at its session-relative
// path, plus the processed deliverables built into a temp folder by the caller-supplied writer
// and shipped under deliverables/. The export GATES (metadata warnings, empty-session checks)
// and all status strings stay at the call site — this is the pure do-it part, testable against
// a fixture session dir.
#include <juce_core/juce_core.h>

#include <functional>

namespace ocap {

struct BundleResult {
    bool ok = false;                 // the zip stream was written
    int  deliverables = 0;           // processed files the writer produced
};

inline BundleResult exportSessionBundle(const juce::File& sessionDir, const juce::File& zipFile,
                                        const std::function<int(const juce::File&)>& writeDeliverables) {
    juce::ZipFile::Builder zip;
    for (const auto& f : sessionDir.findChildFiles(juce::File::findFiles, true))   // raw takes + report.md/html
        zip.addFile(f, 6, f.getRelativePathFrom(sessionDir));
    // Deliverables for the current take (per-mic IRs + the MIX x chosen rates/lengths), built into
    // a temp folder and added under deliverables/ so the zip carries the mix + length/rate variants.
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(sessionDir.getFileName() + "_deliv");
    tmp.deleteRecursively(); tmp.createDirectory();
    BundleResult r;
    r.deliverables = writeDeliverables ? writeDeliverables(tmp) : 0;
    if (r.deliverables > 0)
        for (const auto& f : tmp.findChildFiles(juce::File::findFiles, true))
            zip.addFile(f, 6, "deliverables/" + f.getRelativePathFrom(tmp));
    juce::FileOutputStream os(zipFile);
    if (os.openedOk()) { r.ok = zip.writeToStream(os, nullptr); os.flush(); }
    tmp.deleteRecursively();
    return r;
}

} // namespace ocap
