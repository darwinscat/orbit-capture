// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
// OrbitCapture — the export plan (JUCE-free, std only). De-monolith step 8.
//
// Everything the Export tab DECIDES, with none of what it DOES: toggles + naming inputs go in,
// the exact deliverable file list (relative paths, target rates, truncation lengths, sources)
// comes out. Extracted verbatim from CaptureComponent's writeDeliverablesTo / deliverablePrefix /
// perMicName / mixName / sanitizeName so the pro-lib folder scheme (<rate>/<length>/<name>.wav,
// raw/ copies) and the "Author - Cab - Mic - Pos Dist" naming are headless-testable. The app
// walks the plan and does the I/O (resample, 24-bit write, raw copy) — order is rate-major,
// then source, then length, so a one-entry resample cache hits exactly like the old nested loop.
#include <cmath>
#include <string>
#include <vector>

namespace ocap::exportplan {

inline std::string trimmed(const std::string& v) {
    size_t b = 0, e = v.size();
    while (b < e && (unsigned char)v[b] <= ' ') ++b;
    while (e > b && (unsigned char)v[e - 1] <= ' ') --e;
    return v.substr(b, e - b);
}
// Deliverable-name hygiene: trim, strip filesystem-hostile chars, trim again (UTF-8-safe — only
// ASCII bytes are ever removed).
inline std::string sanitizeName(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : trimmed(s)) if (std::string("/\\:*?\"<>|").find(c) == std::string::npos) out += c;
    return trimmed(out);
}

// Naming inputs: the author/brand + cab model (session fields) and the per-mic file bases
// ("AKG C414 - Cap 1in") built at capture/load time.
struct Naming {
    std::string author, cabModel;
    std::vector<std::string> micFileBases;
};

inline std::string deliverablePrefix(const Naming& n) {                // "Darwin's Cat - PPC212V"
    std::string a = sanitizeName(n.author);   if (a.empty()) a = "OrbitCapture";
    std::string c = sanitizeName(n.cabModel); if (c.empty()) c = "cab";
    return a + " - " + c;
}
inline std::string perMicName(const Naming& n, size_t m) { return deliverablePrefix(n) + " - " + n.micFileBases[m]; }
inline std::string mixName(const Naming& n) {                          // "... - MIX (C414+D112+M160)"
    std::string toks;
    for (size_t m = 0; m < n.micFileBases.size(); ++m) {
        const std::string& base = n.micFileBases[m];
        const size_t dash = base.find(" - ");
        std::string model = trimmed(dash == std::string::npos ? base : base.substr(0, dash));
        const size_t sp = model.rfind(' ');
        toks += (m ? "+" : "") + (sp == std::string::npos ? model : model.substr(sp + 1));
    }
    return deliverablePrefix(n) + " - MIX (" + toks + ")";
}

// The Export tab's toggles, as data.
struct Selection {
    bool len1024 = false, len2048 = false, len4096 = false, len200ms = false, len500ms = false;
    bool rate44 = false, rate48 = false, rate96 = false;
    bool srcMics = false, srcBlend = false, srcRaw = false;
};

constexpr int kMixSource = -1;             // PlannedFile::source value for the mixer MIX

struct PlannedFile {
    std::string relPath;                   // "48kHz/2048/<name>.wav" or "raw/<name> - raw.wav"
    int         source = 0;                // mic index, or kMixSource
    bool        raw = false;               // raw copy: no resample/truncate, srcFileName is set
    std::string srcFileName;               // raw only: "raw.wav" / "raw_micN.wav" in the take dir
    double      targetSr = 0;              // processed only: resample target
    int         lenSamples = 0;            // processed only: truncation length at targetSr
};

// The full deliverable list for one take. numMics = per-mic IR count; the MIX exists only for
// multi-mic takes (the mixer never exists for a single mic).
inline std::vector<PlannedFile> plan(const Selection& sel, const Naming& naming, int numMics) {
    std::vector<PlannedFile> out;
    struct Len { const char* label; bool time; double v; bool on; };
    const Len lens[5] = { { "1024",  false, 1024.0, sel.len1024 }, { "2048",  false, 2048.0, sel.len2048 },
                          { "4096",  false, 4096.0, sel.len4096 },
                          { "200ms", true,  0.2,    sel.len200ms }, { "500ms", true,  0.5,   sel.len500ms } };
    struct Rate { const char* dir; double sr; bool on; };
    const Rate rates[3] = { { "44.1kHz", 44100.0, sel.rate44 }, { "48kHz", 48000.0, sel.rate48 },
                            { "96kHz",  96000.0, sel.rate96 } };
    std::vector<std::pair<std::string, int>> sources;                  // name, source index
    if (sel.srcMics)
        for (int m = 0; m < numMics; ++m) sources.push_back({ perMicName(naming, (size_t)m), m });
    if (sel.srcBlend && numMics >= 2) sources.push_back({ mixName(naming), kMixSource });
    for (const auto& r : rates) {
        if (!r.on) continue;
        for (const auto& src : sources)
            for (const auto& L : lens) {
                if (!L.on) continue;
                PlannedFile f;
                f.relPath = std::string(r.dir) + "/" + L.label + "/" + src.first + ".wav";
                f.source = src.second;
                f.targetSr = r.sr;
                f.lenSamples = L.time ? (int)std::lround(r.sr * L.v) : (int)L.v;
                out.push_back(std::move(f));
            }
    }
    if (sel.srcRaw)                                                    // untouched mic'd sweeps, capture rate
        for (int m = 0; m < numMics; ++m) {
            PlannedFile f;
            f.raw = true;
            f.source = m;
            f.srcFileName = numMics == 1 ? "raw.wav" : "raw_mic" + std::to_string(m + 1) + ".wav";
            f.relPath = "raw/" + perMicName(naming, (size_t)m) + " - raw.wav";
            out.push_back(std::move(f));
        }
    return out;
}

} // namespace ocap::exportplan
