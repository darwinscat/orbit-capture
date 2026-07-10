// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// Headless tests for the IR-import normalizer (session from already-captured files). JUCE-free.
#include <felitronics_test.h>
#include "core/IrImport.h"

#include <cmath>
#include <vector>

using felitronics::test::ok;
using felitronics::test::group;
using namespace ocap::irimport;

namespace
{
SourceIr ir (const char* name, double sr, size_t n, float fill = 0.5f)
{
    SourceIr s; s.name = name; s.sr = sr; s.samples.assign (n, fill);
    return s;
}
}

int main()
{
    std::printf ("orbitcapture ir-import tests\n");

    group ("common rate = the highest source rate");
    {
        const auto set = normalize ({ ir ("a", 48000, 480), ir ("b", 96000, 960) }, 8);
        ok (set.sampleRate == 96000.0, "48k + 96k set lands at 96k");
        ok (set.irs.size() == 2 && set.names[0] == "a" && set.names[1] == "b", "names follow the irs");
        ok (std::fabs (set.irs[0][set.irs[0].size() / 2] - 0.5) < 1e-3, "constant survives the resample");
    }

    group ("equal lengths (the blend engine sums equal-length IRs)");
    {
        const auto set = normalize ({ ir ("short", 48000, 100), ir ("long", 48000, 300) }, 8);
        ok (set.irs[0].size() == 300 && set.irs[1].size() == 300, "all padded to the longest");
        ok (set.irs[0][250] == 0.0, "padding is silence");
        ok (set.irs[1][250] == 0.5, "the long IR keeps its tail");
    }

    group ("caps");
    {
        std::vector<SourceIr> many;
        for (int i = 0; i < 10; ++i) many.push_back (ir ("m", 48000, 64));
        const auto set = normalize (std::move (many), 8);
        ok (set.irs.size() == 8 && set.truncatedCount, "10 files -> 8 mics, flagged");

        const auto lng = normalize ({ ir ("sweep", 48000, 48000 * 5) }, 8, 2.0);
        ok (lng.irs[0].size() == 96000 && lng.truncatedLength, "5 s file capped at 2 s, flagged");

        const auto fine = normalize ({ ir ("ok", 48000, 24000) }, 8, 2.0);
        ok (!fine.truncatedCount && !fine.truncatedLength, "in-range set unflagged");
    }

    group ("degenerate inputs");
    {
        ok (normalize ({}, 8).irs.empty(), "no files -> empty set");
        const auto set = normalize ({ ir ("empty", 48000, 0), ir ("good", 48000, 64) }, 8);
        ok (set.irs.size() == 1 && set.names[0] == "good", "empty/unreadable sources are dropped");
        ok (normalize ({ ir ("norate", 0, 64) }, 8).irs.empty(), "sr<=0 sources are dropped");
    }

    return felitronics::test::report();
}
