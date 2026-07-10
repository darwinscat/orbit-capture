// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
// OrbitCapture — session report generation (juce_core only). De-monolith step 8.
//
// report.md / report.html as PURE STRING BUILDERS: SessionMeta + per-take TakeMeta in, the whole
// document out — extracted verbatim from CaptureComponent's writeReportMd / writeReportHtml, so
// the report content is testable without a widget tree or a session folder on disk. Writing the
// string to <session>/report.{md,html} stays at the call site. One deliberate upgrade over the
// old widget-reading code: rows come through takeFromVar's TakeMeta (via SessionStore), so all
// three legacy take.json generations — including pre-'mics' v0 takes the old raw-JSON loop
// silently skipped — appear in the report.
#include "model/SessionModel.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace ocap::report {

// One report row source: a take dir's name + its (migrated) metadata.
struct TakeRow {
    juce::String name;
    TakeMeta     take;
};

inline juce::String takeTime(const std::string& timestamp) {           // "YYYYMMDD-HHMMSS" -> "HH:MM:SS"
    const juce::String ts(timestamp);
    return ts.length() >= 15 ? ts.substring(9, 11) + ":" + ts.substring(11, 13) + ":" + ts.substring(13, 15)
                             : juce::String();
}

inline juce::String htmlEsc(const juce::String& s) {
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;");
}

inline juce::String reportMd(const juce::String& sessionName, const SessionMeta& s,
                             const juce::String& interfaceName, const std::vector<TakeRow>& rows) {
    auto J = [](const std::string& v) { return juce::String(v); };
    juce::String md;
    md << "# OrbitCapture session: " << sessionName << "\n\n"
       << "- cabinet: " << J(s.cabModel) << " (" << J(s.speakerCount) << "x" << J(s.speakerSizeIn)
       << ", " << J(s.back) << ", " << J(s.speaker) << ", " << J(s.tweeter) << ")\n"
       << "- instrument/enclosure: " << J(s.instrument) << " / " << J(s.enclosure) << "\n"
       << "- amp: " << J(s.amp) << " (" << J(s.ampType) << ")\n"
       << "- room: " << J(s.room) << "\n"
       << "- interface: " << interfaceName << "\n\n"
       << "| take | time | mic | location | position | dist | axis | in | peak dBFS | SNR | delay |\n"
       << "|---|---|---|---|---|---|---|---|---|---|---|\n";
    for (const auto& r : rows)
        for (const auto& m : r.take.mics)
            md << "| " << r.name
               << " | " << takeTime(r.take.timestamp)
               << " | " << J(m.model)
               << " | " << J(m.location)
               << " | " << J(m.position)
               << " | " << J(m.distanceInput)
               << " | " << J(m.axis)
               << " | " << juce::String(m.inputChannel)
               << " | " << juce::String(m.gatePeakDbfs, 1)
               << " | " << juce::String(m.snrDb, 1) << " dB"
               << " | +" << juce::String((juce::int64)m.delaySamples) << " |\n";
    return md;
}

// A styled, self-contained report.html — double-click opens it in any browser on macOS / Windows.
inline juce::String reportHtml(const juce::String& sessionName, const SessionMeta& s,
                               const juce::String& interfaceName, const std::vector<TakeRow>& rows) {
    auto J = [](const std::string& v) { return juce::String(v); };
    auto td = [](const juce::String& v) { return "<td>" + htmlEsc(v) + "</td>"; };
    juce::String h;
    h << "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
      << "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      << "<title>OrbitCapture \xe2\x80\x94 " << htmlEsc(sessionName) << "</title><style>"
      << "body{margin:0;background:#16181d;color:#e6e6ea;font:14px/1.55 -apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;padding:28px}"
      << "h1{font-size:20px;margin:0 0 2px;color:#fff;font-weight:700}h1 span{color:#9778ff}"
      << ".meta{color:#b9a6ff;margin:16px 0 22px;display:grid;gap:3px}.meta b{color:#e6e6ea;font-weight:600}"
      << ".wrap{overflow-x:auto;border:1px solid #2b2f36;border-radius:8px}"
      << "table{border-collapse:collapse;width:100%;font-size:13px}"
      << "th,td{padding:7px 11px;text-align:left;border-bottom:1px solid #23262d;white-space:nowrap}"
      << "th{color:#ff8a3d;font-weight:600;background:#1c1f26;position:sticky;top:0}"
      << "tbody tr:hover td{background:#1b1e25}"
      << ".foot{color:#6b7280;margin-top:20px;font-size:12px}"
      << "</style></head><body>";
    h << "<h1>OrbitCapture <span>\xc2\xb7 " << htmlEsc(sessionName) << "</span></h1>";
    h << "<div class=\"meta\">"
      << "<div><b>Cabinet:</b> " << htmlEsc(J(s.cabModel)) << " (" << htmlEsc(J(s.speakerCount)) << "\xc3\x97" << htmlEsc(J(s.speakerSizeIn))
      << ", " << htmlEsc(J(s.back)) << ", " << htmlEsc(J(s.speaker)) << ", " << htmlEsc(J(s.tweeter)) << ")</div>"
      << "<div><b>Instrument / enclosure:</b> " << htmlEsc(J(s.instrument)) << " / " << htmlEsc(J(s.enclosure)) << "</div>"
      << "<div><b>Amp:</b> " << htmlEsc(J(s.amp)) << " (" << htmlEsc(J(s.ampType)) << ")</div>"
      << "<div><b>Room:</b> " << htmlEsc(J(s.room)) << "</div>"
      << "<div><b>Interface:</b> " << htmlEsc(interfaceName) << "</div></div>";
    h << "<div class=\"wrap\"><table><thead><tr>"
      << "<th>take</th><th>time</th><th>mic</th><th>location</th><th>position</th><th>dist</th>"
      << "<th>axis</th><th>in</th><th>peak dBFS</th><th>SNR</th><th>delay</th></tr></thead><tbody>";
    for (const auto& r : rows)
        for (const auto& m : r.take.mics)
            h << "<tr>" << td(r.name) << td(takeTime(r.take.timestamp))
              << td(J(m.model)) << td(J(m.location))
              << td(J(m.position)) << td(J(m.distanceInput))
              << td(J(m.axis)) << td(juce::String(m.inputChannel))
              << td(juce::String(m.gatePeakDbfs, 1))
              << td(juce::String(m.snrDb, 1) + " dB")
              << td("+" + juce::String((juce::int64)m.delaySamples)) << "</tr>";
    h << "</tbody></table></div>"
      << "<div class=\"foot\">Generated by OrbitCapture \xc2\xb7 Darwin's Cat</div></body></html>";
    return h;
}

} // namespace ocap::report
