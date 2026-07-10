// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
#include "UiSupport.h"
#include <map>

// User-wide editable gear lists (mic / speaker / cab_model / amp) behind a thin store seam:
// today a JSON file in the user's app-data dir, later the catalog DB — the UI only talks to
// this interface, so swapping the backend never touches the combos.
struct ListStore {
    virtual ~ListStore() = default;
    virtual juce::StringArray get(const juce::String& list) = 0;
    virtual void add(const juce::String& list, const juce::String& v) = 0;
    virtual void rename(const juce::String& list, const juce::String& from, const juce::String& to) = 0;
    virtual void remove(const juce::String& list, const juce::String& v) = 0;
    virtual void seed(const juce::String& list, const juce::StringArray& defaults) = 0;
};

class FileListStore : public ListStore {
public:
    FileListStore() {
        file = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("OrbitCapture").getChildFile("lists.json");
        const auto parsed = juce::JSON::parse(file.loadFileAsString());
        if (auto* o = parsed.getDynamicObject())
            for (const auto& p : o->getProperties())
                if (auto* arr = p.value.getArray()) {
                    juce::StringArray sa;
                    for (const auto& v : *arr) sa.add(v.toString());
                    lists[p.name.toString()] = sa;
                }
    }
    juce::StringArray get(const juce::String& l) override {
        auto it = lists.find(l); return it != lists.end() ? it->second : juce::StringArray();
    }
    void add(const juce::String& l, const juce::String& v) override {
        auto& sa = lists[l];
        if (v.isEmpty() || sa.contains(v, true)) return;
        sa.add(v); sa.sortNatural(); save();
    }
    void rename(const juce::String& l, const juce::String& from, const juce::String& to) override {
        auto& sa = lists[l]; const int i = sa.indexOf(from);
        if (i < 0 || to.isEmpty()) return;
        sa.set(i, to); sa.sortNatural(); save();
    }
    void remove(const juce::String& l, const juce::String& v) override {
        auto& sa = lists[l]; const int i = sa.indexOf(v);
        if (i < 0) return;
        sa.remove(i); save();
    }
    void seed(const juce::String& l, const juce::StringArray& defaults) override {
        // Union-merge: introduce each default exactly ONCE (tracked in a hidden "_seeded:"
        // marker) so new app versions add new defaults to an existing lists.json, yet a
        // default the user deliberately deleted never comes back.
        auto& seeded = lists["_seeded:" + l];
        auto& cur    = lists[l];
        bool changed = false;
        for (int i = seeded.size(); --i >= 0;)                          // prune stale factory names (renamed/removed)
            if (!defaults.contains(seeded[i], true)) {
                cur.removeString(seeded[i]); seeded.remove(i); changed = true;
            }
        for (const auto& d : defaults)
            if (!seeded.contains(d, true)) {
                seeded.add(d);
                if (!cur.contains(d, true)) cur.add(d);
                changed = true;
            }
        if (changed) { cur.sortNatural(); save(); }
    }
private:
    void save() {
        auto* o = new juce::DynamicObject();
        for (const auto& [name, sa] : lists) {
            juce::Array<juce::var> a;
            for (const auto& s : sa) a.add(s);
            o->setProperty(name, a);
        }
        file.getParentDirectory().createDirectory();
        file.replaceWithText(juce::JSON::toString(juce::var(o)));
    }
    juce::File file;
    std::map<juce::String, juce::StringArray> lists;
};
