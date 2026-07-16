// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// Headless tests for the mic-set placement/binding rules (de-monolith step 8). JUCE-free —
// these rules used to live inside CaptureComponent and needed a widget tree to exercise.
#include <felitronics_test.h>
#include "model/MicSetModel.h"
#include "model/SessionModel.h"

#include <string>
#include <vector>

using felitronics::test::ok;
using felitronics::test::group;
using namespace ocap::micset;

namespace
{
MicRowSpec grille (const std::string& pos, double distCm, int in = -1, int slot = -1)
{
    return { "grille", pos, distCm * 10.0, in, slot };
}
}

int main()
{
    std::printf ("orbitcapture mic-set model tests\n");

    group ("locLimit: 4 grille / 2 room / 2 rear");
    ok (locLimit ("grille") == 4, "grille limit is 4");
    ok (locLimit ("room") == 2, "room limit is 2");
    ok (locLimit ("rear") == 2, "rear limit is 2");

    group ("countLoc");
    {
        std::vector<MicRowSpec> rows { grille ("Cap Edge", 0), grille ("Center Cap", 0),
                                       { "room", "room", 600, -1, -1 } };
        ok (countLoc (rows, "grille") == 2, "counts matching locations");
        ok (countLoc (rows, "grille", 0) == 1, "except skips the row's own index");
        ok (countLoc (rows, "rear") == 0, "no rear rows");
    }

    group ("lowestFreeInput");
    {
        std::vector<MicRowSpec> rows { grille ("Cap Edge", 0, 0), grille ("Center Cap", 0, 2) };
        ok (lowestFreeInput (rows, 4) == 1, "first gap between taken inputs");
        ok (lowestFreeInput (rows, 1) == -1, "all inputs taken → -1");
        ok (lowestFreeInput (rows, 4, 1) == 1, "except frees that row's input (in2 stays free, in1 first)");
        ok (lowestFreeInput ({}, 0) == -1, "no device inputs → -1");
        std::vector<MicRowSpec> unbound { grille ("Cap Edge", 0, -1) };
        ok (lowestFreeInput (unbound, 2) == 0, "unbound rows (-1) don't block channel 0");
    }

    group ("firstFreeSlot: colour identity survives removals");
    {
        std::vector<MicRowSpec> rows { grille ("Cap Edge", 0, -1, 0), grille ("Center Cap", 0, -1, 2) };
        ok (firstFreeSlot (rows, 8) == 1, "fills the gap left by a removed mic");
        ok (firstFreeSlot ({}, 8) == 0, "empty set starts at slot 0");
        std::vector<MicRowSpec> full;
        for (int s = 0; s < 8; ++s) full.push_back (grille ("Cap Edge", s, -1, s));
        ok (firstFreeSlot (full, 8) == -1, "all slots taken → -1");
    }

    group ("newRowLocation: grille → room → rear → full");
    {
        std::vector<MicRowSpec> rows;
        ok (newRowLocation (rows) == "grille", "empty set: grille first");
        for (int i = 0; i < 4; ++i) rows.push_back (grille ("Cap Edge", i));
        ok (newRowLocation (rows) == "room", "grille full: room next");
        rows.push_back ({ "room", "room", 600, -1, -1 });
        rows.push_back ({ "room", "room", 800, -1, -1 });
        ok (newRowLocation (rows) == "rear", "room full: rear next");
        rows.push_back ({ "rear", "rear", 300, -1, -1 });
        rows.push_back ({ "rear", "rear", 400, -1, -1 });
        ok (newRowLocation (rows).empty(), "4+2+2 = full set: no location");
    }

    group ("placeGrille: first free preferred spot, distance-major");
    {
        std::vector<MicRowSpec> rows;
        auto s1 = placeGrille (rows);
        ok (s1 && s1->position == "Cap Edge" && s1->distCm == 0.0, "empty grid → Cap Edge 0 cm");
        rows.push_back (grille ("Cap Edge", 0));
        auto s2 = placeGrille (rows);
        ok (s2 && s2->position == "Center Cap" && s2->distCm == 0.0, "same distance, next position");
        for (const char* p : { "Center Cap", "Center Cone", "Cone Edge" }) rows.push_back (grille (p, 0));
        auto s3 = placeGrille (rows);
        ok (s3 && s3->position == "Cap Edge" && s3->distCm == 1.0, "0 cm row full → Cap Edge 1 cm");
        // a taken spot means same position within 5 mm — 2 cm away is a different spot
        std::vector<MicRowSpec> far { grille ("Cap Edge", 2) };
        auto s4 = placeGrille (far);
        ok (s4 && s4->position == "Cap Edge" && s4->distCm == 0.0, "same position 2 cm away doesn't block 0 cm");
        // `except` ignores the row being (re)placed itself
        std::vector<MicRowSpec> self { grille ("Cap Edge", 0) };
        auto s5 = placeGrille (self, 0);
        ok (s5 && s5->position == "Cap Edge" && s5->distCm == 0.0, "except: a row doesn't block its own spot");
        // room/rear rows never block grille spots
        std::vector<MicRowSpec> mixed { { "room", "Cap Edge", 0, -1, -1 } };
        auto s6 = placeGrille (mixed);
        ok (s6 && s6->position == "Cap Edge" && s6->distCm == 0.0, "non-grille rows don't block the grid");
        // all 32 preferred spots taken → nullopt
        std::vector<MicRowSpec> packed;
        for (double d : { 0, 1, 2, 3, 5, 8, 10, 15 })
            for (const char* p : { "Cap Edge", "Center Cap", "Center Cone", "Cone Edge" })
                packed.push_back (grille (p, d));
        ok (! placeGrille (packed).has_value(), "grid exhausted → nullopt (caller keeps the row put)");
    }

    group ("guessModel: known mics detected in imported file names");
    {
        const std::vector<std::string> cat { "Shure SM57", "Sennheiser e906", "Sennheiser e609",
                                             "AKG C414", "Royer R-121", "Beyerdynamic M160" };
        ok (guessModel ("YA MES 412 TRAD 906-1", cat) == "Sennheiser e906", "bare digits pick e906 (906-1)");
        ok (guessModel ("cab_SM57_capedge", cat) == "Shure SM57", "exact token, punctuation-blind");
        ok (guessModel ("Mesa e609 close", cat) == "Sennheiser e609", "e609 vs e906 stay distinct");
        ok (guessModel ("R-121 ribbon 2", cat) == "Royer R-121", "hyphenated fingerprint matches");
        ok (guessModel ("take 57", cat).empty(), "2-digit bare number is too ambiguous");
        ok (guessModel ("cab 4140 bright", cat).empty(), "'4140' is not '414' (whole-token only)");
        ok (guessModel ("sm57 or m160", cat).empty(), "two exact hits -> no guess (never a wrong pick)");
        ok (guessModel ("room mic", cat).empty(), "nothing recognizable -> empty");
    }

    group ("sameMicSetup: the capture-replace identity — placement counts, measurements don't");
    {
        auto mk = [] (const char* model, const char* loc, const char* pos, const char* axis,
                      int distMm, int in) {
            ocap::MicMeta m;
            m.model = model; m.location = loc; m.position = pos; m.axis = axis;
            m.distanceMm = distMm; m.inputChannel = in;
            return m;
        };
        const std::vector<ocap::MicMeta> set { mk ("SM57", "grille", "Cap Edge", "on-axis", 20, 1),
                                               mk ("R121", "room",   "room",     "",        600, 2) };
        ok (ocap::sameMicSetup (set, set), "identical sets match");

        auto measured = set;                       // a re-take: new levels/SNR/delay, same physical setup
        measured[0].gatePeakDbfs = -3.2; measured[0].snrDb = 51.0; measured[0].latencySamples = 99;
        measured[1].delaySamples = 240;  measured[1].slot = 5;
        measured[1].distanceInput = "60 cm";       // unit re-format, distanceMm unchanged
        ok (ocap::sameMicSetup (set, measured), "measured fields, slot and distanceInput don't break identity");

        auto v = set; v[0].model = "MD421";
        ok (! ocap::sameMicSetup (set, v), "different model differs");
        v = set; v[0].position = "Center Cap";
        ok (! ocap::sameMicSetup (set, v), "different grid position differs");
        v = set; v[0].axis = "off-axis";
        ok (! ocap::sameMicSetup (set, v), "different axis differs");
        v = set; v[1].distanceMm = 900;
        ok (! ocap::sameMicSetup (set, v), "different distance differs");
        v = set; v[0].inputChannel = 3;
        ok (! ocap::sameMicSetup (set, v), "different input binding differs");
        v = set; v.pop_back();
        ok (! ocap::sameMicSetup (set, v), "different mic count differs");
        const std::vector<ocap::MicMeta> none;
        ok (! ocap::sameMicSetup (none, none), "empty never matches (named-empty/import takes must not pair)");
    }

    return felitronics::test::report();
}
