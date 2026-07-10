// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// Headless tests for the export plan (de-monolith step 8): toggles + naming → the exact
// deliverable file list. JUCE-free — this used to be decided inline in writeDeliverablesTo,
// testable only by clicking Export and reading the Desktop.
#include <felitronics_test.h>
#include "core/ExportPlanner.h"

#include <string>
#include <vector>

using felitronics::test::ok;
using felitronics::test::group;
using namespace ocap::exportplan;

int main()
{
    std::printf ("orbitcapture export-plan tests\n");

    group ("sanitizeName");
    ok (sanitizeName ("  AKG C414  ") == "AKG C414", "trims whitespace");
    ok (sanitizeName ("a/b\\c:d*e?f\"g<h>i|j") == "abcdefghij", "strips filesystem-hostile chars");
    ok (sanitizeName (" / ").empty(), "hostile-only input collapses to empty");

    group ("naming");
    {
        const Naming n { "Darwin's Cat", "Orange PPC212V", { "AKG C414 - Cap 1in", "Shure SM57 - Cone Edge 3in" } };
        ok (deliverablePrefix (n) == "Darwin's Cat - Orange PPC212V", "prefix = author - cab");
        ok (perMicName (n, 0) == "Darwin's Cat - Orange PPC212V - AKG C414 - Cap 1in", "per-mic = prefix - base");
        ok (mixName (n) == "Darwin's Cat - Orange PPC212V - MIX (C414+SM57)",
            "MIX joins the last word of each model");
        const Naming blank { "", "", { "SM57" } };
        ok (deliverablePrefix (blank) == "OrbitCapture - cab", "empty author/cab fall back");
        ok (mixName (blank) == "OrbitCapture - cab - MIX (SM57)", "base without ' - ' is its own token");
    }

    group ("plan: rates × lengths × sources, rate-major order");
    {
        Selection sel;
        sel.len2048 = sel.len500ms = true;                             // the UI defaults
        sel.rate44 = sel.rate48 = sel.rate96 = true;
        sel.srcMics = sel.srcBlend = true;
        const Naming n { "A", "C", { "M1 - p", "M2 - q" } };
        const auto files = plan (sel, n, 2);
        ok (files.size() == 18, "3 rates x (2 mics + MIX) x 2 lengths = 18");
        ok (files[0].relPath == "44.1kHz/2048/A - C - M1 - p.wav", "first file: 44.1 / 2048 / mic1");
        ok (files[0].source == 0 && !files[0].raw, "mic index as source");
        ok (files[0].lenSamples == 2048, "sample lengths are literal");
        ok (files[1].relPath == "44.1kHz/500ms/A - C - M1 - p.wav" && files[1].lenSamples == 22050,
            "500 ms at 44.1 kHz = 22050 samples");
        ok (files[4].source == kMixSource && files[4].relPath == "44.1kHz/2048/A - C - MIX (M1+M2).wav",
            "MIX follows the per-mic sources");
        ok (files[6].relPath == "48kHz/2048/A - C - M1 - p.wav", "next rate starts after the previous");
        ok (files[7].lenSamples == 24000 && files[13].lenSamples == 48000,
            "500 ms scales with the target rate");
        bool rateMajor = true;                                         // every 44.1 file before every 48 file...
        for (size_t i = 1; i < files.size(); ++i)
            if (files[i].targetSr < files[i - 1].targetSr) rateMajor = false;
        ok (rateMajor, "rate-major order (one-slot resample cache stays hot)");
    }

    group ("plan: single mic never gets a MIX");
    {
        Selection sel; sel.len1024 = sel.rate48 = sel.srcMics = sel.srcBlend = true;
        const auto files = plan (sel, { "A", "C", { "M1" } }, 1);
        ok (files.size() == 1 && files[0].source == 0, "srcBlend on, 1 mic: per-mic file only");
    }

    group ("plan: raw copies");
    {
        Selection sel; sel.srcRaw = true;
        const auto one = plan (sel, { "A", "C", { "M1 - p" } }, 1);
        ok (one.size() == 1 && one[0].raw, "raw only when nothing else is selected");
        ok (one[0].srcFileName == "raw.wav", "single mic: unsuffixed raw.wav");
        ok (one[0].relPath == "raw/A - C - M1 - p - raw.wav", "raw/ folder + ' - raw' suffix");
        const auto two = plan (sel, { "A", "C", { "M1", "M2" } }, 2);
        ok (two.size() == 2 && two[0].srcFileName == "raw_mic1.wav" && two[1].srcFileName == "raw_mic2.wav",
            "multi-mic: per-mic raw_micN.wav");
    }

    group ("plan: nothing selected → empty");
    ok (plan ({}, { "A", "C", {} }, 0).empty(), "no toggles, no files");

    return felitronics::test::report();
}
