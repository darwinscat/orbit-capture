// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
// Tests for report generation + bundle assembly (de-monolith step 8, juce_core tier).
// The report used to be built inline from live widgets — now it's SessionMeta + TakeMeta in,
// document out, so the content is checkable without a widget tree.
#include <felitronics_test.h>
#include "persist/ReportWriter.h"
#include "persist/BundleExporter.h"

using felitronics::test::ok;
using felitronics::test::group;

int main()
{
    std::printf ("orbitcapture report/bundle tests\n");

    ocap::SessionMeta s;
    s.cabModel = "Orange PPC212V"; s.speakerCount = "2"; s.speakerSizeIn = "12";
    s.back = "closed"; s.speaker = "V30"; s.tweeter = "no tweeter";
    s.instrument = "guitar"; s.enclosure = "cab"; s.amp = "Super Crush 100";
    s.ampType = "solid state"; s.room = "treated";

    ocap::TakeMeta t;
    t.timestamp = "20260710-153045";
    ocap::MicMeta m1; m1.model = "AKG C414"; m1.location = "grille"; m1.position = "Cap Edge";
    m1.distanceInput = "1 cm"; m1.axis = "On-axis"; m1.inputChannel = 1;
    m1.gatePeakDbfs = -12.345; m1.snrDb = 52.71; m1.delaySamples = 17;
    ocap::MicMeta m2; m2.model = "R&D <proto>"; m2.location = "room"; m2.inputChannel = 2;
    t.mics = { m1, m2 };
    const std::vector<ocap::report::TakeRow> rows { { "take01", t } };

    group ("takeTime");
    ok (ocap::report::takeTime ("20260710-153045") == "15:30:45", "YYYYMMDD-HHMMSS -> HH:MM:SS");
    ok (ocap::report::takeTime ("garbage").isEmpty(), "short/invalid timestamp -> empty");

    group ("report.md");
    {
        const auto md = ocap::report::reportMd ("PPC212V_20260710", s, "Scarlett 18i20", rows);
        ok (md.contains ("# OrbitCapture session: PPC212V_20260710"), "session name in the title");
        ok (md.contains ("- cabinet: Orange PPC212V (2x12, closed, V30, no tweeter)"), "cabinet line");
        ok (md.contains ("- interface: Scarlett 18i20"), "interface line");
        ok (md.contains ("| take01 | 15:30:45 | AKG C414 | grille | Cap Edge | 1 cm | On-axis | 1 | -12.3 | 52.7 dB | +17 |"),
            "mic row: fields + 1-decimal numbers + +delay");
        ok (md.contains ("| take01 | 15:30:45 | R&D <proto> |"), "one row per mic of the take");
    }

    group ("report.html");
    {
        const auto h = ocap::report::reportHtml ("PPC212V_20260710", s, "A&B <if>", rows);
        ok (h.contains ("<td>R&amp;D &lt;proto&gt;</td>"), "cell content is HTML-escaped");
        ok (h.contains ("A&amp;B &lt;if&gt;"), "interface name is HTML-escaped");
        ok (h.contains ("<td>-12.3</td>") && h.contains ("<td>52.7 dB</td>") && h.contains ("<td>+17</td>"),
            "numbers match the md table");
        ok (h.startsWith ("<!doctype html>") && h.contains ("</body></html>"), "self-contained document");
    }

    group ("session bundle zip");
    {
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ocap_bundle_test");
        tmp.deleteRecursively(); tmp.createDirectory();
        const auto sess = tmp.getChildFile ("sess"); sess.getChildFile ("take01").createDirectory();
        sess.getChildFile ("take01/take.json").replaceWithText ("{}");
        sess.getChildFile ("report.md").replaceWithText ("# r");
        const auto zipF = tmp.getChildFile ("out.zip");

        const auto res = ocap::exportSessionBundle (sess, zipF, [] (const juce::File& d) {
            d.getChildFile ("48kHz/2048").createDirectory();
            d.getChildFile ("48kHz/2048/a.wav").replaceWithText ("x");
            return 1;
        });
        ok (res.ok && res.deliverables == 1, "bundle written, deliverable counted");
        juce::ZipFile zr (zipF);
        juce::StringArray names;
        for (int i = 0; i < zr.getNumEntries(); ++i) names.add (zr.getEntry (i)->filename);
        ok (names.contains ("take01/take.json") && names.contains ("report.md"),
            "session files at session-relative paths");
        ok (names.contains ("deliverables/48kHz/2048/a.wav"), "deliverables under deliverables/");

        const auto res2 = ocap::exportSessionBundle (sess, tmp.getChildFile ("out2.zip"),
                                                     [] (const juce::File&) { return 0; });
        juce::ZipFile zr2 (tmp.getChildFile ("out2.zip"));
        ok (res2.ok && res2.deliverables == 0 && zr2.getNumEntries() == 2,
            "no deliverables -> raw session only");
        tmp.deleteRecursively();
    }

    return felitronics::test::report();
}
