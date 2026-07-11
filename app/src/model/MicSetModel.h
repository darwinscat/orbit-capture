// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
// OrbitCapture — mic-set placement/binding rules (JUCE-free, std only). De-monolith step 8.
//
// The rules that govern the Capture tab's mic rows, extracted verbatim from CaptureComponent
// (locLimit / countLoc / lowestFreeInput / placeGrille / slot + location assignment in addMicRow)
// so they are headless-testable: limits 4 grille / 2 room / 2 rear, next-free-input binding,
// first-free colour slot, and the preferred-spot search that keeps every new grille mic on a FREE
// grid position. The UI builds MicRowSpec views of its rows at the call site — widgets never
// enter this header.
#include <cctype>
#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ocap::micset {

// A widget-free view of one mic row — just what the rules read.
struct MicRowSpec {
    std::string location;        // "grille" | "room" | "rear"
    std::string position;        // grid spot for grille mics ("Cap Edge", ...); else free text
    double      distanceMm = 0;  // 0 when the dist field is empty
    int         inputChannel = -1;   // 0-based device input; -1 = unbound
    int         slot = -1;           // colour slot (mic identity across takes)
};

inline int locLimit(const std::string& loc) { return loc == "grille" ? 4 : 2; }

// Rows at `loc`, excluding index `except` (pass the row's own index when re-validating a change).
inline int countLoc(std::span<const MicRowSpec> rows, const std::string& loc, int except = -1) {
    int n = 0;
    for (int i = 0; i < (int)rows.size(); ++i)
        if (i != except && rows[(size_t)i].location == loc) ++n;
    return n;
}

// First device input not taken by another row; -1 when everything's in use (or numInputs == 0).
inline int lowestFreeInput(std::span<const MicRowSpec> rows, int numInputs, int except = -1) {
    for (int ch = 0; ch < numInputs; ++ch) {
        bool used = false;
        for (int i = 0; i < (int)rows.size(); ++i)
            if (i != except && rows[(size_t)i].inputChannel == ch) used = true;
        if (!used) return ch;
    }
    return -1;
}

// First free colour slot in [0, maxSlots); -1 when the set is full.
inline int firstFreeSlot(std::span<const MicRowSpec> rows, int maxSlots) {
    for (int s = 0; s < maxSlots; ++s) {
        bool used = false;
        for (const auto& r : rows) if (r.slot == s) used = true;
        if (!used) return s;
    }
    return -1;
}

// Where a NEW row goes: grille while there's room, then room, then rear; "" = set is full.
inline std::string newRowLocation(std::span<const MicRowSpec> rows) {
    std::string loc = "grille";
    if (countLoc(rows, loc) >= locLimit(loc)) loc = countLoc(rows, "room") < 2 ? "room" : "rear";
    if (countLoc(rows, loc) >= locLimit(loc)) return {};
    return loc;
}

struct GrilleSpot { std::string position; double distCm = 0; };

// The first FREE preferred grid spot (distance-major order: every position at 0 cm before 1 cm...).
// A spot is taken when another grille row sits on the same position within 5 mm. nullopt = no free
// preferred spot — the caller leaves the row where it is.
inline std::optional<GrilleSpot> placeGrille(std::span<const MicRowSpec> rows, int except = -1) {
    static const char* prefPos[] = { "Cap Edge", "Center Cap", "Center Cone", "Cone Edge" };
    static const double prefCm[] = { 0, 1, 2, 3, 5, 8, 10, 15 };
    for (double dcm : prefCm)
        for (const auto* pp : prefPos) {
            bool taken = false;
            for (int i = 0; i < (int)rows.size(); ++i) {
                const auto& r = rows[(size_t)i];
                if (i != except && r.location == "grille" && r.position == pp
                    && std::abs(r.distanceMm - dcm * 10.0) < 5.0) taken = true;
            }
            if (!taken) return GrilleSpot { pp, dcm };
        }
    return std::nullopt;
}

// ---- mic-model detection from a file name (imported IRs) ----------------------------------
// "YA MES 412 TRAD 906-1.wav" + a catalog holding "Sennheiser e906" → "Sennheiser e906".
// A catalog model's fingerprint is its last word ("e906", "SM57", "C414"); a file-name token
// matches it exactly (case/punctuation-blind), or — weaker — matches its bare digits when
// they're 3+ long ("906" → e906, but a lone "57" stays ambiguous). Exact beats digits; any
// ambiguity (two models claiming the same token) yields no guess — never a silent wrong pick.

inline std::vector<std::string> alnumTokens(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (std::isalnum((unsigned char)c)) cur += (char)std::tolower((unsigned char)c);
        else if (!cur.empty()) { out.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

inline std::string guessModel(const std::string& fileName, const std::vector<std::string>& catalog) {
    const auto toks = alnumTokens(fileName);
    std::string exact, digits;
    int exactHits = 0, digitHits = 0;
    for (const auto& model : catalog) {
        const auto mt = alnumTokens(model);
        if (mt.empty()) continue;
        const std::string& fp = mt.back();                            // the fingerprint token
        std::string num;
        for (char c : fp) if (std::isdigit((unsigned char)c)) num += c;
        bool hitExact = false, hitNum = false;
        for (const auto& t : toks) {
            if (t == fp) hitExact = true;
            else if (num.size() >= 3 && t == num) hitNum = true;
        }
        if (hitExact && exact != model) { exact = model; ++exactHits; }
        else if (hitNum && digits != model) { digits = model; ++digitHits; }
    }
    if (exactHits == 1) return exact;
    if (exactHits == 0 && digitHits == 1) return digits;
    return {};                                                         // none or ambiguous
}

} // namespace ocap::micset
