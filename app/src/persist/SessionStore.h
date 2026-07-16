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
        writeTakeInto (dir, take, raw, irs, sr);
        return dir;
    }

    // Re-record over an existing take dir (the capture-replace flow: same mic setup, user confirmed).
    // The old capture is wiped and the new one written under the SAME take number — audio, take.json
    // and numbering end up as if this capture had been the original.
    juce::File replaceTake (const juce::File& takeDir, const TakeMeta& take,
                           const std::vector<std::vector<double>>& raw,
                           const std::vector<std::vector<double>>& irs, double sr) const {
        takeDir.deleteRecursively();
        writeTakeInto (takeDir, take, raw, irs, sr);
        return takeDir;
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

    // The mixer is the take's channel EDITOR: channels can be appended (imported IRs) and removed.
    // Both keep the on-disk contract loadTake expects — N == 1 uses the un-suffixed ir.wav/raw.wav
    // names, N > 1 uses _micN — so files are renamed across the 1 <-> many boundary and renumbered
    // on removal. take.json is read-modify-written (additive keys survive); mic = mics[0] mirror
    // kept in sync.

    // Append one channel: writes ir_mic<N+1>.wav (no raw — imports have none) + extends mics[]/mix[].
    // Returns false when the take.json is unreadable.
    bool appendChannel (const juce::File& takeDir, const MicMeta& mic,
                        const std::vector<double>& ir, double sr) const {
        const auto f = takeDir.getChildFile ("take.json");
        auto v = juce::JSON::parse (f.loadFileAsString());
        auto* o = v.getDynamicObject();
        if (! o) return false;
        auto* mics = v.getProperty ("mics", juce::var()).getArray();
        if (! mics) {                                                  // a named EMPTY take gains its first channel
            o->setProperty ("mics", juce::Array<juce::var>());
            mics = v.getProperty ("mics", juce::var()).getArray();
        }
        const int n = mics->size();
        if ((double) v.getProperty ("sample_rate", 0.0) <= 0.0)
            o->setProperty ("sample_rate", sr);                        // the first channel sets the take's rate
        if (n == 1) {                                                  // 1 -> 2: the un-suffixed files gain _mic1
            takeDir.getChildFile ("ir.wav").moveFileTo (takeDir.getChildFile ("ir_mic1.wav"));
            takeDir.getChildFile ("raw.wav").moveFileTo (takeDir.getChildFile ("raw_mic1.wav"));
        }
        oc::wav_write_mono_f32 (takeDir.getChildFile (n == 0 ? juce::String ("ir.wav")
                                                              : "ir_mic" + juce::String (n + 1) + ".wav")
                                    .getFullPathName().toStdString(), ir, sr);
        mics->add (micToVar (mic));
        if (auto* mix = v.getProperty ("mix", juce::var()).getArray()) {
            auto* mo = new juce::DynamicObject();
            stripToVar (mo, StripParams {});
            mix->add (juce::var (mo));
        }
        o->setProperty ("mic", (*mics)[0]);                            // keep the v1 mirror in sync
        f.replaceWithText (juce::JSON::toString (v));
        return true;
    }

    // Remove channel `idx`: deletes its ir/raw files, renumbers the rest, drops mics[idx]/mix[idx].
    // Refuses to remove the last channel (delete the take instead). Returns false when refused.
    bool removeChannel (const juce::File& takeDir, int idx) const {
        const auto f = takeDir.getChildFile ("take.json");
        auto v = juce::JSON::parse (f.loadFileAsString());
        auto* o = v.getDynamicObject();
        if (! o) return false;
        auto* mics = v.getProperty ("mics", juce::var()).getArray();
        if (! mics || mics->size() < 2 || idx < 0 || idx >= mics->size()) return false;
        const int n = mics->size();
        auto nameOf = [] (const char* base, int i, int total) {        // the on-disk naming rule
            return juce::String (base) + (total == 1 ? juce::String() : "_mic" + juce::String (i + 1)) + ".wav";
        };
        for (const char* base : { "ir", "raw" }) {
            takeDir.getChildFile (nameOf (base, idx, n)).deleteFile();
            for (int i = idx + 1; i < n; ++i)                          // shift the tail down one slot
                takeDir.getChildFile (nameOf (base, i, n))
                       .moveFileTo (takeDir.getChildFile (nameOf (base, i - 1, n)));
            if (n - 1 == 1)                                            // 2 -> 1: back to the un-suffixed names
                takeDir.getChildFile (juce::String (base) + "_mic1.wav")
                       .moveFileTo (takeDir.getChildFile (juce::String (base) + ".wav"));
        }
        mics->remove (idx);
        if (auto* mix = v.getProperty ("mix", juce::var()).getArray())
            if (idx < mix->size()) mix->remove (idx);
        o->setProperty ("mic", (*mics)[0]);                            // keep the v1 mirror in sync
        f.replaceWithText (juce::JSON::toString (v));
        return true;
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
    // The single take-dir writer behind saveTake/replaceTake: the raw/ir WAV naming rule
    // (N==1 un-suffixed, N>1 _micN) + take.json.
    void writeTakeInto (const juce::File& dir, const TakeMeta& take,
                        const std::vector<std::vector<double>>& raw,
                        const std::vector<std::vector<double>>& irs, double sr) const {
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
    }

    juce::File root_;
};

} // namespace ocap
