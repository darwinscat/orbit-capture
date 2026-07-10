// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
// OrbitCapture — session + take persistence (juce_core File I/O). De-monolith step 6. Extracts the
// mechanics VERBATIM from Main.cpp's saveTakeToSession / loadTake / saveMixToTake / scanTakes /
// saveSessionJson / loadSessionJson / createSessionDir / sessionsRoot, so existing session folders on
// disk keep working unchanged. The ctor takes the sessions ROOT dir directly (production passes
// sessionsRoot(); tests inject a temp dir) so this is fully testable off the real app-data location.
#include "model/SessionVar.h"

#include <oc/wav.hpp>

#include <juce_core/juce_core.h>

#include <algorithm>
#include <optional>
#include <vector>

namespace ocap {

// The result of loading one take dir: its metadata + the per-mic IRs read back off disk (+ their sample
// rate). Names/colours/UI labels stay app-side — this is the pure persistence contract.
struct LoadedTake {
    TakeMeta                         take;
    std::vector<std::vector<double>> irs;
    double                           sampleRate = 0.0;
};

class SessionStore {
public:
    explicit SessionStore (juce::File root) : root_ (std::move (root)) { root_.createDirectory(); }

    const juce::File& root() const noexcept { return root_; }

    // All session dirs directly under root, newest-modified first (mirrors refreshSessionBox's scan).
    std::vector<juce::File> scanSessions() const {
        auto ds = root_.findChildFiles (juce::File::findDirectories, false);
        std::sort (ds.begin(), ds.end(), [] (const juce::File& a, const juce::File& b) {
            return a.getLastModificationTime() > b.getLastModificationTime(); });
        std::vector<juce::File> out; out.reserve ((std::size_t) ds.size());
        for (auto& d : ds) out.push_back (d);
        return out;
    }

    // A new "<name>_<YYYYMMDD-HHMM>" session dir under root (mirrors createSessionDir's naming/sanitizing;
    // the cab-model-name fallback is a UI concern and stays in Main.cpp — callers pass the final name).
    juce::File createSession (juce::String name) const {
        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M");
        name = name.trim();
        if (name.isEmpty()) name = "session";
        name = name.replaceCharacter (' ', '-').replaceCharacter ('/', '-');
        const auto dir = root_.getChildFile (name + "_" + stamp);
        dir.createDirectory();
        return dir;
    }

    // All "take*" dirs under a session dir, sorted by folder name (mirrors scanTakes).
    std::vector<juce::File> scanTakes (const juce::File& sessionDir) const {
        auto ds = sessionDir.findChildFiles (juce::File::findDirectories, false, "take*");
        std::sort (ds.begin(), ds.end(), [] (const juce::File& a, const juce::File& b) {
            return a.getFileName() < b.getFileName(); });
        std::vector<juce::File> out; out.reserve ((std::size_t) ds.size());
        for (auto& d : ds) out.push_back (d);
        return out;
    }

    void saveSessionJson (const juce::File& dir, const SessionMeta& s) const {
        dir.getChildFile ("session.json").replaceWithText (juce::JSON::toString (sessionToVar (s)));
    }

    SessionMeta loadSessionJson (const juce::File& dir) const {
        return sessionFromVar (juce::JSON::parse (dir.getChildFile ("session.json").loadFileAsString()));
    }

    // Owns takeNN numbering + the raw/ir WAV naming rule (N==1 keeps the old un-suffixed names, N>1 gets
    // _micN — mirrors saveTakeToSession exactly), then writes take.json. Returns the created takeNN dir.
    juce::File saveTake (const juce::File& sessionDir, const TakeMeta& take,
                        const std::vector<std::vector<double>>& raw,
                        const std::vector<std::vector<double>>& irs, double sr) const {
        sessionDir.createDirectory();
        int nn = 1;
        while (sessionDir.getChildFile ("take" + juce::String (nn).paddedLeft ('0', 2)).exists()) ++nn;
        const auto dir = sessionDir.getChildFile ("take" + juce::String (nn).paddedLeft ('0', 2));
        dir.createDirectory();

        const int N = (int) irs.size();
        for (int m = 0; m < N; ++m) {
            const juce::String suffix = N == 1 ? juce::String() : "_mic" + juce::String (m + 1);
            if (m < (int) raw.size())
                oc::wav_write_mono_f32 (dir.getChildFile ("raw" + suffix + ".wav").getFullPathName().toStdString(),
                                        raw[(std::size_t) m], sr);
            oc::wav_write_mono_f32 (dir.getChildFile ("ir" + suffix + ".wav").getFullPathName().toStdString(),
                                    irs[(std::size_t) m], sr);
        }

        dir.getChildFile ("take.json").replaceWithText (juce::JSON::toString (takeToVar (take)));
        return dir;
    }

    // Metadata only — take.json through the same legacy migrations, no WAV reads (reports, labels).
    TakeMeta loadTakeMeta (const juce::File& takeDir) const {
        return takeFromVar (juce::JSON::parse (takeDir.getChildFile ("take.json").loadFileAsString()));
    }

    // Parses take.json + reads back the per-mic IR WAVs (mirrors loadTake's mechanics; the UI-only bits —
    // display names/colours/file-base sanitizing — stay in Main.cpp).
    LoadedTake loadTake (const juce::File& takeDir) const {
        LoadedTake lt;
        const auto v = juce::JSON::parse (takeDir.getChildFile ("take.json").loadFileAsString());
        lt.take = takeFromVar (v);
        const int N = (int) lt.take.mics.size();
        for (int m = 0; m < N; ++m) {
            const juce::String suffix = N == 1 ? juce::String() : "_mic" + juce::String (m + 1);
            const auto w = oc::wav_read (takeDir.getChildFile ("ir" + suffix + ".wav").getFullPathName().toStdString());
            if (! w.ok || w.ch.empty()) continue;
            lt.sampleRate = w.sr;
            lt.irs.emplace_back (w.ch[0].begin(), w.ch[0].end());
        }
        return lt;
    }

    // READ-MODIFY-WRITE: parse the existing take.json, set only mix/master/mono_filter, rewrite — so
    // additive/unknown keys (future schema fields) survive untouched (mirrors saveMixToTake exactly;
    // never a full TakeMeta->var rewrite, which would silently drop anything this code doesn't model yet).
    // `master` is optional to mirror Main.cpp's `if (masterStrip) ...` gate: a single-mic take has no
    // Master strip (rebuildMixer only builds one for >=2 mics), so passing nullopt there leaves an
    // existing take.json's "master" key (if any — e.g. from a fresh-capture write) untouched rather than
    // stamping a bogus default Master object onto a take that never had one.
    void saveMix (const juce::File& takeDir, const std::vector<StripParams>& mix,
                 const std::optional<MasterParams>& master, const std::optional<StripParams>& mono) const {
        const auto f = takeDir.getChildFile ("take.json");
        auto v = juce::JSON::parse (f.loadFileAsString());
        auto* o = v.getDynamicObject();
        if (! o) return;

        juce::Array<juce::var> mixArr;
        for (const auto& s : mix) {
            auto* mo = new juce::DynamicObject();
            stripToVar (mo, s);
            mixArr.add (juce::var (mo));
        }
        o->setProperty ("mix", mixArr);
        if (master) {
            auto* mo = new juce::DynamicObject();
            masterToVar (mo, *master);
            o->setProperty ("master", juce::var (mo));
        }
        if (mono) {
            auto* mo = new juce::DynamicObject();
            stripToVar (mo, *mono);
            o->setProperty ("mono_filter", juce::var (mo));
        }

        f.replaceWithText (juce::JSON::toString (v));
    }

private:
    juce::File root_;
};

} // namespace ocap
