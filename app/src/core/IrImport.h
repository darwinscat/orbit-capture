// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
// OrbitCapture — import already-captured IR files as a take (JUCE-free, std only).
//
// The maths of "session from files": N decoded mono IRs (any mix of sample rates) in, one
// mixer-ready set out — resampled to a single common rate (the HIGHEST among the sources, so
// nothing is downsampled), padded to equal length (the blend engine sums equal-length IRs),
// capped to the mic-set maximum and to a sane IR duration. File decoding and take.json
// persistence stay at the call site (the UI decodes via JUCE, then saves the result through
// SessionStore::saveTake exactly like a captured take — imported takes are indistinguishable
// downstream: same mixer, same audition, same export).
#include "core/IrDeliverable.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ocap::irimport {

struct SourceIr {
    std::string        name;      // file base name — becomes the (forever-editable) mic label
    std::vector<float> samples;   // decoded mono audio
    double             sr = 0;
};

struct ImportSet {
    std::vector<std::vector<double>> irs;   // equal length, common rate
    std::vector<std::string>         names; // parallel to irs
    double sampleRate = 0;
    bool   truncatedCount = false;          // more sources than maxMics came in
    bool   truncatedLength = false;         // a source was longer than maxSeconds
};

// maxSeconds caps a mistakenly-imported full recording (a real IR is well under a second);
// the cap is applied AT the common rate, after resampling.
inline ImportSet normalize(std::vector<SourceIr> src, int maxMics, double maxSeconds = 2.0) {
    ImportSet out;
    src.erase(std::remove_if(src.begin(), src.end(),
                             [](const SourceIr& s) { return s.samples.empty() || s.sr <= 0; }),
              src.end());
    if (src.empty()) return out;
    if ((int)src.size() > maxMics) { src.resize((size_t)maxMics); out.truncatedCount = true; }
    double sr = 0;
    for (const auto& s : src) sr = std::max(sr, s.sr);
    out.sampleRate = sr;
    const size_t cap = (size_t)std::llround(sr * maxSeconds);
    size_t maxLen = 0;
    for (auto& s : src) {
        std::vector<double> ir;
        if (std::abs(s.sr - sr) < 1.0) ir.assign(s.samples.begin(), s.samples.end());
        else ir = resampleIR(s.samples, s.sr, sr);
        if (ir.size() > cap) { ir.resize(cap); out.truncatedLength = true; }
        maxLen = std::max(maxLen, ir.size());
        out.irs.push_back(std::move(ir));
        out.names.push_back(std::move(s.name));
    }
    for (auto& ir : out.irs) ir.resize(maxLen, 0.0);                   // equal length: pad with silence
    return out;
}

} // namespace ocap::irimport
