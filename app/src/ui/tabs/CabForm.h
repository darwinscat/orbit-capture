// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
#include "ui/UiSupport.h"
#include "ui/Stores.h"
#include "model/SessionModel.h"

// The cabinet / speaker / amp form — the Export tab's quality gate (used to be a modal "edit"
// dialog). Owns the session-describing combos + the user-wide gear lists ([...] managers), and
// speaks SessionMeta to the outside: toMeta / fromMeta / clear / validate / missingFields.
// Every change fires onChanged (the orchestrator autosaves + refreshes the export status).
// NO silent defaults — every catalog value must be a conscious pick ("select ..." prompts).
struct CabForm : juce::Component {
    static constexpr int kHeight = 238;                     // fixed form height (the rows in resized())

    juce::Label cabHeader;
    juce::ComboBox instrumentBox, enclosureBox, countBox, sizeBox, backBox, ampTypeBox, roomBox;
    juce::ComboBox tweeterBox;                 // 3-state: unselected ("tweeter?") / no tweeter / tweeter
    juce::ComboBox cabModel, speaker, amp;                  // user-wide gear lists (ListStore-backed)
    juce::TextButton spkDots, cabDots, ampDots;             // [...] list managers
    juce::Label lTimes;
    ListStore& listStore;
    std::function<void()> onChanged;                        // any field edit (autosave + validation)

    explicit CabForm(ListStore& ls) : listStore(ls) {
        auto selCombo = [this](juce::ComboBox& c, const juce::StringArray& items, const juce::String& prompt) {
            for (int i = 0; i < items.size(); ++i) c.addItem(items[i], i + 1);
            c.setTextWhenNothingSelected(prompt);
            addAndMakeVisible(c);
        };
        selCombo(instrumentBox, vocab::instrument, "select instrument");
        selCombo(enclosureBox, vocab::enclosure, "select enclosure");
        selCombo(backBox, vocab::back, "select back");
        selCombo(countBox, vocab::speakerCount, "n"); selCombo(sizeBox, vocab::speakerSize, "size");
        selCombo(tweeterBox, vocab::tweeter, "tweeter?");
        lTimes.setText("x", juce::dontSendNotification); lTimes.setJustificationType(juce::Justification::centred); addAndMakeVisible(lTimes);
        // gear combos fed by the user-wide ListStore; the [...] button manages the list
        auto listCombo = [this](juce::ComboBox& c, juce::TextButton& dots, const juce::String& list,
                                const juce::String& placeholder, const juce::String& sel) {
            c.setTextWhenNothingSelected(placeholder);                 // read-only: additions go through [...]
            addAndMakeVisible(c);
            dots.setButtonText("..."); addAndMakeVisible(dots);
            dots.onClick = [this, &c, &dots, list] { manageList(list, c, dots); };
            refreshListCombo(c, list, sel);
        };
        listStore.seed("speaker", vocab::speakerSeed);
        listCombo(speaker,  spkDots, "speaker",   "speaker (e.g. V30)", {});
        listCombo(cabModel, cabDots, "cab_model", "cab model (e.g. Orange PPC212V)", {});
        listCombo(amp,      ampDots, "amp",       "amp (e.g. Orange Super Crush 100)", {});
        selCombo(ampTypeBox, vocab::ampType, "select amp type"); selCombo(roomBox, vocab::room, "select room");
        cabHeader.setText("Cabinet, speaker & amp", juce::dontSendNotification);
        cabHeader.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        cabHeader.setColour(juce::Label::textColourId, brand::lilac);
        addAndMakeVisible(cabHeader);
        for (auto* c : { &instrumentBox, &enclosureBox, &backBox, &countBox, &sizeBox, &tweeterBox,
                         &ampTypeBox, &roomBox, &cabModel, &speaker, &amp })
            c->onChange = [this] { if (onChanged) onChanged(); };      // autosave + live validation, wired outside
    }

    void resized() override {
        auto r = getLocalBounds();
        auto row = [&r](int h) { return r.removeFromTop(h); };
        auto two = [](juce::Rectangle<int> a, juce::Component& L, juce::Component& R) {
            L.setBounds(a.removeFromLeft(a.getWidth() / 2 - 4)); a.removeFromLeft(8); R.setBounds(a); };
        cabHeader.setBounds(row(16)); row(4);
        two(row(26), instrumentBox, enclosureBox); row(6);
        { auto a = row(26); countBox.setBounds(a.removeFromLeft(48)); lTimes.setBounds(a.removeFromLeft(16));
          sizeBox.setBounds(a.removeFromLeft(60)); a.removeFromLeft(10); tweeterBox.setBounds(a); } row(6);
        { auto a = row(26); spkDots.setBounds(a.removeFromRight(26)); a.removeFromRight(4); speaker.setBounds(a); } row(6);
        { auto a = row(26); cabDots.setBounds(a.removeFromRight(26)); a.removeFromRight(4); cabModel.setBounds(a); } row(6);
        { auto a = row(26); ampDots.setBounds(a.removeFromRight(26)); a.removeFromRight(4); amp.setBounds(a); } row(6);
        two(row(26), ampTypeBox, backBox); row(6);
        roomBox.setBounds(row(26));
    }

    // ---- SessionMeta bridge (the cab-form fields; author/distUnit live outside the form) ----
    void toMeta(ocap::SessionMeta& s) const {
        s.instrument = instrumentBox.getText().toStdString(); s.enclosure = enclosureBox.getText().toStdString();
        s.cabModel = cabModel.getText().toStdString(); s.speaker = speaker.getText().toStdString();
        s.speakerCount = countBox.getText().toStdString(); s.speakerSizeIn = sizeBox.getText().toStdString();
        s.tweeter = tweeterBox.getText().toStdString(); s.back = backBox.getText().toStdString();
        s.amp = amp.getText().toStdString(); s.ampType = ampTypeBox.getText().toStdString();
        s.room = roomBox.getText().toStdString();
    }
    void fromMeta(const ocap::SessionMeta& s) {
        auto selByText = [](juce::ComboBox& c, const juce::String& t) {
            c.setSelectedId(0, juce::dontSendNotification);
            for (int i = 0; i < c.getNumItems(); ++i)
                if (c.getItemText(i) == t) { c.setSelectedId(c.getItemId(i), juce::dontSendNotification); break; }
        };
        selByText(instrumentBox, s.instrument); selByText(enclosureBox, s.enclosure);
        selByText(countBox, s.speakerCount); selByText(sizeBox, s.speakerSizeIn);
        selByText(tweeterBox, s.tweeter); selByText(backBox, s.back);
        selByText(ampTypeBox, s.ampType); selByText(roomBox, s.room);
        refreshListCombo(cabModel, "cab_model", s.cabModel);
        refreshListCombo(speaker, "speaker", s.speaker);
        refreshListCombo(amp, "amp", s.amp);
    }
    void clear() {                                          // a blank session (no silent defaults)
        for (auto* c : { &instrumentBox, &enclosureBox, &countBox, &sizeBox, &tweeterBox, &backBox,
                         &ampTypeBox, &roomBox, &cabModel, &speaker, &amp })
            c->setSelectedId(0, juce::dontSendNotification);
        cabModel.setText({}, juce::dontSendNotification);
        speaker.setText({}, juce::dontSendNotification);
        amp.setText({}, juce::dontSendNotification);
    }
    // Red-outline every empty required field (the visual half of the export gate).
    void validate() {
        auto mark = [](juce::ComboBox& c, bool ok) {
            c.setColour(juce::ComboBox::outlineColourId,
                        ok ? juce::Colour(0xff3a3f47) : juce::Colours::red.withAlpha(0.9f));
            c.repaint();
        };
        for (auto* c : { &instrumentBox, &enclosureBox, &backBox, &countBox, &sizeBox,
                         &tweeterBox, &ampTypeBox, &roomBox })
            mark(*c, c->getSelectedId() != 0);
        mark(cabModel, cabModel.getText().trim().isNotEmpty());
        mark(speaker,  speaker.getText().trim().isNotEmpty());
        mark(amp,      amp.getText().trim().isNotEmpty());
    }
    // The session-field half of the export warnings (the per-take mic warnings live outside).
    juce::StringArray missingFields() const {
        juce::StringArray m;
        auto need = [&m](const juce::ComboBox& c, const char* name) { if (c.getSelectedId() == 0) m.add(name); };
        need(instrumentBox, "instrument"); need(enclosureBox, "enclosure"); need(backBox, "back");
        need(countBox, "speaker count"); need(sizeBox, "speaker size"); need(tweeterBox, "tweeter");
        if (cabModel.getText().trim().isEmpty()) m.add("cab model");
        if (speaker.getText().trim().isEmpty())  m.add("speaker");
        if (amp.getText().trim().isEmpty())      m.add("amp");
        need(ampTypeBox, "amp type"); need(roomBox, "room");
        return m;
    }

    // --- user-wide gear-list management (the [...] button next to a combo) ---
    void refreshListCombo(juce::ComboBox& c, const juce::String& list, const juce::String& sel = {}) {
        const juce::String keep = sel.isNotEmpty() ? sel : c.getText();
        c.clear(juce::dontSendNotification);
        int id = 1, selId = 0;
        for (const auto& s : listStore.get(list)) { c.addItem(s, id); if (s == keep) selId = id; ++id; }
        if (selId > 0) c.setSelectedId(selId, juce::dontSendNotification);
        else if (keep.isNotEmpty()) c.setText(keep, juce::dontSendNotification);
    }
    void manageList(const juce::String& list, juce::ComboBox& c, juce::Component& target) {
        const juce::String cur = c.getText();
        juce::PopupMenu m;
        m.addItem(1, "Add...");
        m.addItem(2, "Edit \"" + cur + "\"...", cur.isNotEmpty());
        m.addSeparator();
        m.addItem(3, "Delete \"" + cur + "\"", cur.isNotEmpty());
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&target), [this, list, &c, cur](int r) {
            if (r == 1)
                textPrompt("Add to the " + list + " list", "", [this, list, &c](juce::String v) {
                    listStore.add(list, v); refreshListCombo(c, list, v); if (onChanged) onChanged(); });
            else if (r == 2)
                textPrompt("Edit " + list, cur, [this, list, &c, cur](juce::String v) {
                    listStore.rename(list, cur, v); refreshListCombo(c, list, v); if (onChanged) onChanged(); });
            else if (r == 3)
                juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Delete",
                    "Delete \"" + cur + "\" from the " + list + " list?", "Delete", "Cancel", this,
                    juce::ModalCallbackFunction::create([this, list, &c, cur](int ok) {
                        if (ok == 1) { listStore.remove(list, cur); c.setText({}, juce::dontSendNotification);
                                       refreshListCombo(c, list); if (onChanged) onChanged(); }
                    }));
        });
    }
};
