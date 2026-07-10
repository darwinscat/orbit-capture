// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko & Alisa Lafoks. Part of OrbitCapture — see LICENSE.
#pragma once
// OrbitCapture — CaptureComponent, the app orchestrator (header + 4 tabs, model<->UI sync,
// capture/review/export wiring). Moved verbatim out of Main.cpp (de-monolith step 8); Main.cpp
// is now just the JUCE application shell.
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>        // dsp::Convolution for live input monitoring through the IR
#include "BinaryData.h"        // embedded brand assets (catlogo.svg)
#include "oc/sweep.hpp"
#include "oc/deconv.hpp"
#include "oc/post.hpp"
#include "oc/gate.hpp"
#include "oc/wav.hpp"
#include "oc/fft.hpp"          // oc::convolve for the audition
#include <felitronics/analysis/offline/SpectrumCurve.h>   // spectrum display curves (core)
#include <vector>
#include <span>
#include <algorithm>
#include <map>
#include <atomic>
#include <array>
#include <cstdint>
#include <cmath>
#include <limits>

#include "ui/Widgets.h"

#include "ui/tabs/AudioTab.h"
#include "ui/tabs/CaptureTab.h"
#include "ui/tabs/ReviewTab.h"
#include "ui/tabs/ExportTab.h"

#include "core/BlendKernels.h"
#include "core/AuditionRenderer.h"
#include "core/ExportPlanner.h"
#include "core/IrDeliverable.h"
#include "core/IrImport.h"
#include "core/AutoAlign.h"
#include "core/BlendEngine.h"
#include "core/CapturePipeline.h"
#include "model/MixVar.h"
#include "model/MicSetModel.h"
#include "persist/SessionStore.h"
#include "persist/ReportWriter.h"
#include "persist/BundleExporter.h"
#include "audio/AudioEngine.h"

class CaptureComponent : public juce::Component,
                         public juce::Timer,
                         private juce::AsyncUpdater {
public:
    CaptureComponent() {
        deviceManager.initialiseWithDefaultDevices(2, 2);
        selector.reset(new juce::AudioDeviceSelectorComponent(
            deviceManager, 1, 32, 1, 2, false, false, false, false));
        audioTab.selector = selector.get();
        audioTab.addAndMakeVisible(*selector);

        listStore.seed("mic", vocab::mics);                            // CabForm seeds "speaker"

        takeTab.grid.onSelect = [this](const juce::String& p, double d) {      // empty cell -> move the ACTIVE mic (grille only)
            if (takeTab.micRows.empty() || active().location.getText() != "grille") return;
            active().position = p;
            active().dist.setText(juce::String(d, (d == (double)(int)d) ? 0 : 1), juce::sendNotification);
        };
        takeTab.grid.onDotSelect = [this](int id) { setActiveRow(id); };
        takeTab.grid.onDotDrag = [this](int id, const juce::String& p, double d) {   // drag = direct manipulation
            setActiveRow(id);
            auto& r = active();
            r.position = p;
            r.dist.setText(juce::String(d, (d == (double)(int)d) ? 0 : 1), juce::sendNotification);
        };
        takeTab.addMicBtn.setButtonText("+ add mic");
        takeTab.addMicBtn.onClick = [this] { addMicRow(); };

        takeTab.captureButton.setButtonText("Capture");
        takeTab.captureButton.setColour(juce::TextButton::buttonColourId, brand::orange.darker(0.28f));
        takeTab.captureButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        takeTab.captureButton.onClick = [this] { startCapture(); };   // capture is cheap; the gates live at Export
        takeTab.noiseButton.setButtonText("Play noise (set mic level to the green zone)");
        takeTab.noiseButton.setColour(juce::TextButton::buttonColourId, brand::violet.darker(0.18f));
        takeTab.noiseButton.setColour(juce::TextButton::buttonOnColourId, brand::violet);
        takeTab.noiseButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        takeTab.noiseButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        takeTab.noiseButton.setClickingTogglesState(true);
        takeTab.noiseButton.onClick = [this] { engine.noiseOn.store(takeTab.noiseButton.getToggleState()); updateCalibVerdict(); };
        takeTab.calibVerdict.setJustificationType(juce::Justification::centred);
        takeTab.calibVerdict.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        takeTab.status.setText("Place the mics, set levels with the noise, Capture. Every take lands in the session.",
                       juce::dontSendNotification);
        takeTab.status.setJustificationType(juce::Justification::topLeft);

        // ---- session management (lives in the header's top-right) + the session-fields dialog ----
        sessionBox.setTextWhenNothingSelected("session");              // the session picker
        sessionBox.onChange = [this] {
            const int i = sessionBox.getSelectedId() - 1;
            if (i >= 0 && i < (int)sessionList.size() && sessionList[(size_t)i] != sessionDir)
                switchToSession(sessionList[(size_t)i]);
        };
        addAndMakeVisible(sessionBox);
        // Setup dialog (gear button): session-wide distance units + advanced review toggles.
        for (int i = 0; i < vocab::unit.size(); ++i) distUnit.addItem(vocab::unit[i], i + 1);
        distUnit.setSelectedId(1, juce::dontSendNotification);         // cm — session-wide distance unit
        distUnit.onChange = [this] { onDistUnitChanged(); };
        unitLbl.setText("Distance units (cm / in)", juce::dontSendNotification);
        setupCloseBtn.onClick = [this] {
            if (auto* dw = setupCloseBtn.findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(0);
        };
        for (juce::Component* c : { (juce::Component*)&unitLbl, (juce::Component*)&distUnit,
                                    (juce::Component*)&setupCloseBtn })
            setupPanel.addAndMakeVisible(c);
        setupPanel.onLayout = [this] {
            auto r = setupPanel.getLocalBounds().reduced(16);
            auto row = [&r](int h) { return r.removeFromTop(h); };
            { auto a = row(26); unitLbl.setBounds(a.removeFromLeft(180)); a.removeFromLeft(8); distUnit.setBounds(a.removeFromLeft(80)); }
            row(18);
            { auto a = row(30); setupCloseBtn.setBounds(a.removeFromRight(90)); }
        };
        setupPanel.setSize(320, 120);
        setupBtn.setButtonText(juce::String::fromUTF8("\xe2\x9a\x99"));   // gear glyph
        setupBtn.setTooltip("Setup: distance units + advanced review toggles");
        setupBtn.onClick = [this] { showSetupDialog(); };
        addAndMakeVisible(setupBtn);
        newSessionBtn.setButtonText("+");
        addAndMakeVisible(newSessionBtn);
        newSessionBtn.onClick = [this] {
            saveSessionJson();                   // persist the current one
            textPrompt("New session name", "", [this](juce::String nm) {
                exportTab.cab.clear();           // a blank session; the cabinet is described in Export
                createSessionDir(nm);
                tabs.setCurrentTabIndex(3);      // jump to Export to fill in the cabinet + amp
                validateCabForm();
                updateSessionLabel();
            });
        };
        // The cabinet/amp form (ui/tabs/CabForm.h) sits at the top of Export (the quality gate);
        // any field edit autosaves to session.json + revalidates the red outlines.
        exportTab.cab.onChanged = [this] { saveSessionJson(); validateCabForm(); };

        for (auto& p : engine.chPeak) p.store(0.0f);

        // ---- header + tabs ----
        addAndMakeVisible(header);
        addMicRow();                             // mic 1 — every take has at least one mic
        // ---- Review / Audition tab: play a DI riff through the last captured IR ----
        auto decodeDI = [](const void* d, int sz) {
            std::vector<float> v;
            juce::WavAudioFormat fmt;
            std::unique_ptr<juce::AudioFormatReader> rd(fmt.createReaderFor(new juce::MemoryInputStream(d, (size_t)sz, false), true));
            if (rd != nullptr && rd->lengthInSamples > 0) {
                juce::AudioBuffer<float> b((int)rd->numChannels, (int)rd->lengthInSamples);
                rd->read(&b, 0, (int)rd->lengthInSamples, 0, true, true);
                v.assign(b.getReadPointer(0), b.getReadPointer(0) + b.getNumSamples());
            }
            return v;
        };
        factoryClips.push_back({ "cats-hard-day",      decodeDI(BinaryData::cats_wav,   BinaryData::cats_wavSize),   48000.0 });
        factoryClips.push_back({ "deep-space",         decodeDI(BinaryData::space_wav,  BinaryData::space_wavSize),  48000.0 });
        factoryClips.push_back({ "eleven-light-years", decodeDI(BinaryData::eleven_wav, BinaryData::eleven_wavSize), 48000.0 });
        engine.audition.reserve(3800000);
        engine.conv.reserve(3800000);                                         // stable capacity: the audio thread streams it
        diFormats.registerBasicFormats();
        scanUserSamples();                                        // app-data /samples — user clips persist
        rebuildDiBox({});
        reviewTab.loadDiBtn.setButtonText("Load file...");
        reviewTab.loadDiBtn.onClick = [this] {                              // audition the user's OWN DI track
            diChooser = std::make_unique<juce::FileChooser>("Load a DI track", juce::File(),
                                                            "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
            diChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this](const juce::FileChooser& fc) {
                    const auto f = fc.getResult();
                    if (f == juce::File()) return;
                    std::unique_ptr<juce::AudioFormatReader> rd(diFormats.createReaderFor(f));
                    if (rd == nullptr || rd->lengthInSamples <= 0 || rd->sampleRate <= 0) {
                        reviewTab.reviewInfo.setText("Could not read " + f.getFileName(), juce::dontSendNotification); return;
                    }
                    f.copyFileTo(samplesDir().getChildFile(f.getFileName()));   // user samples persist across sessions
                    // std::min, NOT juce::jmin: an explicit jmin<int64> makes GCC instantiate the
                    // juce::dsp SIMDRegister<long long> overload candidate, which doesn't exist on Linux.
                    const int n = (int)std::min<juce::int64>(rd->lengthInSamples, (juce::int64)(rd->sampleRate * 60.0));
                    juce::AudioBuffer<float> b((int)rd->numChannels, n);
                    rd->read(&b, 0, n, 0, true, true);
                    DiClip c; c.name = f.getFileNameWithoutExtension(); c.sr = rd->sampleRate;
                    c.samples.assign(b.getReadPointer(0), b.getReadPointer(0) + n);
                    if (b.getNumChannels() > 1)                    // fold stereo to mono
                        for (int i = 0; i < n; ++i) c.samples[(size_t)i] = 0.5f * (b.getReadPointer(0)[i] + b.getReadPointer(1)[i]);
                    userClips.push_back(std::move(c));
                    rebuildDiBox(userClips.back().name);
                    reviewTab.reviewInfo.setText("Loaded " + f.getFileName() + " (" + juce::String((double)n / rd->sampleRate, 1)
                                     + " s" + juce::String(rd->lengthInSamples > (juce::int64)n ? ", capped at 60 s" : "")
                                     + "). Play it dry or through the IR.", juce::dontSendNotification);
                });
        };
        reviewTab.bypassBtn.setTooltip("Bypass the cab — audition/monitor the DI dry. Toggle it live while playing to A/B (level-matched).");
        reviewTab.bypassBtn.onClick = [this] { if (engine.conv.mode.load() != 0) reloadLiveIR(); };   // dry/wet A/B, live
        reviewTab.playBtn.setTooltip("Play the sample through the current mix / stop. Live monitoring has its own toggle.");
        reviewTab.playBtn.onClick = [this] {
            if (samplePlaying()) { engine.audition.stop(); if (engine.conv.mode.load() == 1) engine.conv.stop(); }
            else startWetAudition();
            reviewTab.playBtn.setPlaying(samplePlaying());
        };
        reviewTab.takesCap.setText("TAKES", juce::dontSendNotification);
        reviewTab.takesCap.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        reviewTab.takesCap.setColour(juce::Label::textColourId, brand::lilac);
        reviewTab.playLiveBtn.setButtonText("Play live");
        reviewTab.playLiveBtn.setClickingTogglesState(true);
        reviewTab.playLiveBtn.setColour(juce::TextButton::buttonOnColourId, brand::orange.darker(0.1f));
        reviewTab.playLiveBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        reviewTab.playLiveBtn.setTooltip("Monitor the selected input live through the current mix.");
        reviewTab.playLiveBtn.onClick = [this] {
            const bool on = reviewTab.playLiveBtn.getToggleState();
            if (on) { engine.audition.stop(); reloadLiveIR(); engine.conv.setMode(2); }
            else engine.conv.stop();
        };
        reviewTab.recBtn.setClickingTogglesState(true);
        reviewTab.recBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red.darker(0.1f));
        reviewTab.recBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        reviewTab.recBtn.setTooltip("Record the DRY live input into your own sample (saved to the User group). Starts Play live if it isn't running.");
        reviewTab.recBtn.onClick = [this] { reviewTab.recBtn.getToggleState() ? startSampleRecording() : finishSampleRecording(); };
        reviewTab.deleteSampleBtn.setTooltip("Delete the selected USER sample from disk (factory samples stay).");
        reviewTab.deleteSampleBtn.onClick = [this] { deleteSelectedUserSample(); };
        reviewTab.liveInBox.setTextWhenNothingSelected("input");
        reviewTab.liveInBox.onChange = [this] { engine.liveChannel.store(juce::jmax(0, reviewTab.liveInBox.getSelectedId() - 1)); };
        reviewTab.loopToggle.onClick = [this] { engine.audition.loop.store(reviewTab.loopToggle.getToggleState()); };
        reviewTab.loopToggle.setToggleState(true, juce::dontSendNotification); engine.audition.loop.store(true);   // loop by default
        reviewTab.reviewInfo.setJustificationType(juce::Justification::centredLeft);   // a slim status line above the mixer
        reviewTab.reviewInfo.setFont(juce::FontOptions(12.0f));
        reviewTab.reviewInfo.setColour(juce::Label::textColourId, juce::Colours::grey);
        reviewTab.reviewInfo.setText("Capture a cab on the Capture tab, then play a DI riff through it here to check the sound.",
                           juce::dontSendNotification);
        reviewTab.takeBox.setTextWhenNothingSelected("takes");
        reviewTab.takeBox.onChange = [this] { if (reviewTab.takeBox.getSelectedId() > 0) loadTake(reviewTab.takeBox.getSelectedId() - 1); };
        reviewTab.deleteTakeBtn.setButtonText("x");
        reviewTab.deleteTakeBtn.onClick = [this] {
            if (currentTake < 0 || currentTake >= (int)takeDirs.size()) return;
            const auto d = takeDirs[(size_t)currentTake];
            juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Delete take",
                "Delete " + d.getFileName() + " from the session? (its files are removed)", "Delete", "Cancel", this,
                juce::ModalCallbackFunction::create([this, d](int ok) {
                    if (ok != 1) return;
                    d.deleteRecursively();
                    scanTakes();
                    if (!takeDirs.empty()) loadTake((int)takeDirs.size() - 1);
                    else clearReviewState();
                }));
        };
        // The graph's view menu (gear overlay, top-left) — grows more options later.
        reviewTab.spectrumView.onGearMenu = [this](juce::Rectangle<int> screenArea) {
            juce::PopupMenu m;
            m.addItem(1, "Live spectrum analyser", true, analyzerOn);
            m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(screenArea), [this](int r) {
                if (r == 1) { analyzerOn = !analyzerOn; updateLiveSpectrum(); }
            });
        };
        reviewTab.spectrumView.onFilterDrag = [this](double hpHz, int hpDb, double lpHz, int lpDb) {   // graph → the active strip
            MixStrip* a = activeStrip ? activeStrip : reviewTab.masterStrip.get();
            if (a == nullptr) return;
            a->hpfHz = hpHz; a->hpfSlopeDb = hpDb; a->lpfHz = lpHz; a->lpfSlopeDb = lpDb;
            refreshSpectrum(); if (engine.conv.mode.load() != 0) reloadLiveIR();
        };
        reviewTab.spectrumView.onFilterDragEnd = [this] { saveMixToTake(); };
        reviewTab.spectrumView.onPickTrace = [this](int idx) {                      // click a mic curve → its strip; the composite → Master
            if (idx >= 0 && idx < (int)reviewTab.mixRows.size()) setActiveStrip(reviewTab.mixRows[(size_t)idx].get());
            else if (reviewTab.masterStrip) setActiveStrip(reviewTab.masterStrip.get());
        };
        reviewTab.addChannelBtn.setTooltip("Add already-captured IR file(s) to THIS take as new channels (onset-aligned to the set).");
        reviewTab.addChannelBtn.onClick = [this] { appendIrFiles(); };
        reviewTab.spectrumView.onPickLegend = [this](int i) {          // legend row → its strip (last row = MIX/Master)
            if (i >= 0 && i < (int)reviewTab.mixRows.size()) setActiveStrip(reviewTab.mixRows[(size_t)i].get());
            else if (reviewTab.masterStrip) setActiveStrip(reviewTab.masterStrip.get());
        };
        reviewTab.importIrsBtn.setButtonText("New take...");
        reviewTab.importIrsBtn.setTooltip("Create a new empty take (you name it), then add IR files via the console's [+].");
        reviewTab.importIrsBtn.onClick = [this] { newEmptyTake(); };

        // ---- Export tab (ui/tabs/ExportTab.h owns the widgets; the buttons are wired here) ----
        exportTab.authorField.onReturnKey = [this] { saveSessionJson(); };
        exportTab.authorField.onFocusLost = [this] { saveSessionJson(); };
        exportTab.exportBtn.setButtonText("Export to folder...");
        exportTab.exportBtn.setColour(juce::TextButton::buttonColourId, brand::orange.darker(0.28f));
        exportTab.exportBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        exportTab.exportBtn.onClick = [this] {
            exportChooser = std::make_unique<juce::FileChooser>("Export deliverables into folder",
                juce::File::getSpecialLocation(juce::File::userDesktopDirectory), "");
            exportChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                [this](const juce::FileChooser& fc) { const auto d = fc.getResult(); if (d != juce::File()) doExport(d); });
        };
        exportTab.bundleBtn.setButtonText("Export session bundle (zip: all takes + report)");
        exportTab.bundleBtn.setColour(juce::TextButton::buttonColourId, brand::violet.darker(0.2f));
        exportTab.bundleBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        exportTab.bundleBtn.onClick = [this] { exportBundle(); };

        tabs.addTab("1  Audio",   juce::Colour(0xff23252b), &audioTab,  false);   // numbered: the capture workflow order
        tabs.addTab("2  Capture", juce::Colour(0xff23252b), &takeTab,   false);
        tabs.addTab("3  Mixer",   juce::Colour(0xff23252b), &reviewTab, false);
        tabs.addTab("4  Export",  juce::Colour(0xff23252b), &exportTab, false);
        tabs.setCurrentTabIndex(1);
        addAndMakeVisible(tabs);
        // guided workflow: a "-> next step" button at the bottom of each tab (tabs stay clickable)
        const juce::Colour navCol = brand::violet.darker(0.25f);
        audioTab.navToCapture.setButtonText(juce::String::fromUTF8("Capture  \xe2\x86\x92"));
        audioTab.navToCapture.setColour(juce::TextButton::buttonColourId, navCol);
        audioTab.navToCapture.onClick = [this] { tabs.setCurrentTabIndex(1); };
        takeTab.navToReview.setButtonText(juce::String::fromUTF8("Mixer  \xe2\x86\x92"));
        takeTab.navToReview.setColour(juce::TextButton::buttonColourId, navCol);
        takeTab.navToReview.setEnabled(false);                                  // lights up after a capture
        takeTab.navToReview.onClick = [this] { tabs.setCurrentTabIndex(2); };
        reviewTab.navToExport.setButtonText(juce::String::fromUTF8("Export  \xe2\x86\x92"));
        reviewTab.navToExport.setColour(juce::TextButton::buttonColourId, navCol);
        reviewTab.navToExport.onClick = [this] { tabs.setCurrentTabIndex(3); };
        sessionBox.toFront(false); newSessionBtn.toFront(false); setupBtn.toFront(false);   // above the tab bar

        loadLatestSessionOrNew();                // default: resume the last session (auto-create only when none)
        engine.onCaptureComplete = [this] { triggerAsyncUpdate(); };
        deviceManager.addAudioCallback(&engine);
        startTimerHz(20);
        setWantsKeyboardFocus(true);             // 1-4 jump between tabs (unless a text field has focus)
    }
    ~CaptureComponent() override { stopTimer(); deviceManager.removeAudioCallback(&engine); }

    bool keyPressed(const juce::KeyPress& k) override {
        const int idx = juce::String("1234").indexOfChar(k.getTextCharacter());   // a focused editor eats the digit first
        if (idx >= 0 && !k.getModifiers().isCommandDown()) { tabs.setCurrentTabIndex(idx); return true; }
        return false;
    }

    void resized() override {
        auto r = getLocalBounds();
        header.setBounds(r.removeFromTop(62));
        header.clickRight = 1 << 30;                                       // whole header is the site link again
        tabs.setBounds(r);
        // session management sits on the right of the tab-bar row
        auto sb = juce::Rectangle<int>(r.getRight() - 410, r.getY(), 404, tabs.getTabBarDepth()).withSizeKeepingCentre(400, 24);
        setupBtn.setBounds(sb.removeFromRight(30)); sb.removeFromRight(4);        // gear last
        newSessionBtn.setBounds(sb.removeFromRight(26)); sb.removeFromRight(4);
        sessionBox.setBounds(sb);   // ~336 wide (2x the old ~140)
    }

    // The quality gate lives at EXPORT: session fields + every saved take's metadata must be
    // complete before a bundle leaves the app. Capture itself is never blocked by paperwork.
    juce::StringArray exportWarnings() const {
        juce::StringArray m = exportTab.cab.missingFields();
        for (const auto& d : takeDirs) {
            const auto v = juce::JSON::parse(d.getChildFile("take.json").loadFileAsString());
            if (auto* arr = v.getProperty("mics", juce::var()).getArray())
                for (int mm = 0; mm < arr->size(); ++mm) {
                    if ((*arr)[mm].getProperty("model", "").toString().isEmpty())
                        m.add(d.getFileName() + ": mic" + juce::String(mm + 1) + " model");
                    if ((*arr)[mm].getProperty("axis", "").toString().isEmpty())
                        m.add(d.getFileName() + ": mic" + juce::String(mm + 1) + " axis");
                }
        }
        return m;
    }

    // Red-outline every empty required cab field + surface the full warning list under Export.
    void validateCabForm() {
        exportTab.cab.validate();
        refreshExportStatus();
    }
    void refreshExportStatus() {
        const auto w = exportWarnings();
        if (w.isEmpty())
            exportTab.exportStatus.setText("All metadata complete — ready to export.", juce::dontSendNotification);
        else
            exportTab.exportStatus.setText("Fill before export (" + juce::String(w.size()) + "): " + w.joinIntoString(", "),
                                 juce::dontSendNotification);
    }

    void timerCallback() override {
        if (engine.noiseOn.load() && ! takeTab.noiseButton.isShowing()) stopNoise();  // noise is a Capture-tab tool — kill it if you left the tab
        if (auto* dev = deviceManager.getCurrentAudioDevice()) {
            const auto names = dev->getInputChannelNames();
            if (names.size() != lastNumInNames || dev->getName() != lastDevName) {
                lastNumInNames = names.size(); lastDevName = dev->getName(); inNames = names;
                for (auto& rp : takeTab.micRows) {                             // refresh every mic row's input combo
                    auto& c = rp->input;
                    const int keep = c.getSelectedId();
                    c.clear(juce::dontSendNotification);
                    for (int i = 0; i < names.size(); ++i) c.addItem(juce::String(i + 1) + ": " + names[i], i + 1);
                    if (keep > 0 && keep <= names.size()) c.setSelectedId(keep, juce::dontSendNotification);
                }
                for (auto& rp : takeTab.micRows)                               // unbound rows grab the next free input
                    if (rp->input.getSelectedId() == 0) {
                        const int free = lowestFreeInput(rp.get());
                        if (free >= 0) rp->input.setSelectedId(free + 1, juce::dontSendNotification);
                    }
                syncActiveChannel();
                { const int keepLive = reviewTab.liveInBox.getSelectedId();      // Play-live input selector follows the device
                  reviewTab.liveInBox.clear(juce::dontSendNotification);
                  for (int i = 0; i < names.size(); ++i) reviewTab.liveInBox.addItem(juce::String(i + 1) + ": " + names[i], i + 1);
                  if (keepLive > 0 && keepLive <= names.size()) reviewTab.liveInBox.setSelectedId(keepLive, juce::dontSendNotification);
                  else if (names.size() > 0) reviewTab.liveInBox.setSelectedId(1, juce::dontSendNotification);
                  engine.liveChannel.store(juce::jmax(0, reviewTab.liveInBox.getSelectedId() - 1)); }
            }
        }
        const int n = juce::jmin(engine.kMaxMeter, lastNumInNames);
        std::vector<float> lin((size_t)juce::jmax(0, n));
        for (int c = 0; c < n; ++c) lin[(size_t)c] = engine.chPeak[c].exchange(0.0f);
        for (auto& rp : takeTab.micRows) {                                     // each mic's history follows ITS input
            const int ch = rp->input.getSelectedId() - 1;
            rp->hist.push((ch >= 0 && ch < n) ? lin[(size_t)ch] : 0.0f);
        }
        updateCalibVerdict();
        updateLiveSpectrum();
        if (engine.rec.recording.load()) {
            if (engine.conv.mode.load() != 2 || engine.rec.full()) finishSampleRecording();   // live stopped / buffer full
            else reviewTab.reviewInfo.setText("REC " + juce::String((double)engine.rec.len.load() / engine.sampleRate, 1)
                                            + " s - press Rec again to save.", juce::dontSendNotification);
        }
        reviewTab.playBtn.setPlaying(samplePlaying());                 // ▶/■ follows the actual stream state
    }
    // The SAMPLE is audible (pre-rendered audition, or the DI-through-IR stream; mode 2 = live monitor).
    bool samplePlaying() const { return engine.audition.playing.load() || engine.conv.mode.load() == 1; }
    // FFT the playing output (audition / live monitor) into the Review analyser overlay.
    void updateLiveSpectrum() {
        const bool onReview = tabs.getCurrentTabIndex() == 2;
        if (onReview && analyzerOn && !lastIRs.empty() && (engine.audition.playing.load() || engine.conv.mode.load() != 0)) {
            static constexpr int F = 2048;
            std::vector<float> buf((size_t)F);
            const uint32_t w = engine.specW.load(std::memory_order_relaxed);
            for (int i = 0; i < F; ++i) {
                const float win = 0.5f - 0.5f * std::cos(6.2831853f * (float)i / (float)(F - 1));   // Hann
                buf[(size_t)i] = engine.specRing[(w - (uint32_t)F + (uint32_t)i) & (engine.kSpecRing - 1)] * win;
            }
            reviewTab.spectrumView.setLiveSpectrum(SpectrumView::makeCurve(buf, engine.sampleRate, true));
        } else {
            reviewTab.spectrumView.setLiveSpectrum({});
        }
    }

    // One shared read of the whole set during noise calibration: are all mics in the green zone?
    void updateCalibVerdict() {
        if (!engine.noiseOn.load()) { if (takeTab.calibVerdict.getText().isNotEmpty()) takeTab.calibVerdict.setText({}, juce::dontSendNotification); return; }
        juce::StringArray lows, hots; int inZone = 0, bound = 0;
        for (int i = 0; i < (int)takeTab.micRows.size(); ++i) {
            if (takeTab.micRows[(size_t)i]->input.getSelectedId() == 0) continue;
            ++bound;
            const float pk = takeTab.micRows[(size_t)i]->hist.peakDb();
            if (pk < -26.0f)      lows.add("mic" + juce::String(i + 1));
            else if (pk > -12.0f) hots.add("mic" + juce::String(i + 1));
            else ++inZone;
        }
        if (bound == 0) return;
        if (lows.isEmpty() && hots.isEmpty()) {
            takeTab.calibVerdict.setColour(juce::Label::textColourId, juce::Colours::limegreen);
            takeTab.calibVerdict.setText("all " + juce::String(bound) + " mic" + (bound > 1 ? "s" : "") + " in the green zone", juce::dontSendNotification);
        } else {
            takeTab.calibVerdict.setColour(juce::Label::textColourId, juce::Colours::orange);
            juce::String t;
            if (!hots.isEmpty())  t << hots.joinIntoString(", ") << " too hot";
            if (!lows.isEmpty())  t << (t.isEmpty() ? "" : "  |  ") << lows.joinIntoString(", ") << " too quiet";
            takeTab.calibVerdict.setText(t, juce::dontSendNotification);
        }
    }

private:
    // Bind the run: which rows record on which channels. Capture is pre-validated by the gate;
    // a test run tolerates anything (unbound rows skipped; nothing bound -> in1).
    void prepareRun(bool allowDupInputs) {                             // test runs may double up an input (UI/mixer testing)
        engine.rtNumMics = 0;
        for (int i = 0; i < (int)takeTab.micRows.size() && engine.rtNumMics < kMaxMics; ++i) {
            const int ch = takeTab.micRows[(size_t)i]->input.getSelectedId() - 1;
            if (ch < 0) continue;
            bool dup = false;
            for (int m = 0; m < engine.rtNumMics; ++m) if (engine.rtChans[m] == ch) dup = true;
            if (dup && !allowDupInputs) continue;
            engine.rtChans[engine.rtNumMics] = ch; rtRowIdx[engine.rtNumMics] = i; ++engine.rtNumMics;
        }
        if (engine.rtNumMics == 0) { engine.rtChans[0] = 0; rtRowIdx[0] = 0; engine.rtNumMics = 1; }
        if (engine.sweep.empty()) engine.prepareSweep();
        engine.rtTotal = (int)engine.sweep.size();
        for (int m = 0; m < engine.rtNumMics; ++m) engine.recordedM[(size_t)m].assign((size_t)engine.rtTotal, 0.0f);
        engine.playPos = 0;
    }
    // Noise is the Capture-tab level-set tool: kill it AND un-toggle the button (no silent-but-still-on state).
    void stopNoise() {
        engine.noiseOn.store(false);
        takeTab.noiseButton.setToggleState(false, juce::dontSendNotification);
        updateCalibVerdict();
    }
    void startCapture() {
        const auto problems = captureProblems();                       // Capture is always clickable; problems pop up here
        if (!problems.isEmpty()) { showProblemsDialog(problems); return; }
        prepareRun(false);
        stopNoise();                                                   // the sweep plays now — kill the level-set noise (no auto-resume after)
        takeTab.status.setText("Capturing " + juce::String(engine.rtNumMics) + " mic(s), one sweep...", juce::dontSendNotification);
        engine.capturing.store(true, std::memory_order_release);
    }

    // ---- mic-row model ----
    MicRowUI& active() { return *takeTab.micRows[(size_t)activeRow]; }
    const MicRowUI& activeC() const { return *takeTab.micRows[(size_t)activeRow]; }
    int indexOf(const MicRowUI* rw) const {
        for (int i = 0; i < (int)takeTab.micRows.size(); ++i) if (takeTab.micRows[(size_t)i].get() == rw) return i;
        return -1;
    }
    double rowDistanceMm(const MicRowUI& r) const {
        const double v = r.dist.getText().getDoubleValue();
        return distIsInches() ? v * 25.4 : v * 10.0;
    }
    bool distIsInches() const { return distUnit.getText() == "in"; }
    juce::String distUnitText() const { return distIsInches() ? "in" : "cm"; }
    // The placement/binding rules live in model/MicSetModel.h (headless-tested); the UI hands
    // them a widget-free snapshot of the rows and applies what comes back.
    std::vector<ocap::micset::MicRowSpec> rowSpecs() const {
        std::vector<ocap::micset::MicRowSpec> v; v.reserve(takeTab.micRows.size());
        for (const auto& m : takeTab.micRows)
            v.push_back({ m->location.getText().toStdString(), m->position.toStdString(),
                          rowDistanceMm(*m), m->input.getSelectedId() - 1, m->slot });
        return v;
    }
    static int locLimit(const juce::String& loc) { return ocap::micset::locLimit(loc.toStdString()); }
    int countLoc(const juce::String& loc, int except = -1) const {
        return ocap::micset::countLoc(rowSpecs(), loc.toStdString(), except);
    }
    void syncActiveChannel() {
        if (!takeTab.micRows.empty()) captureChannel.store(juce::jmax(0, activeC().input.getSelectedId() - 1));
    }
    int lowestFreeInput(const MicRowUI* except = nullptr) const {      // 0-based; -1 when everything's taken
        return ocap::micset::lowestFreeInput(rowSpecs(), inNames.size(), indexOf(except));
    }
    void setActiveRow(int i) {
        activeRow = juce::jlimit(0, juce::jmax(0, (int)takeTab.micRows.size() - 1), i);
        syncActiveChannel();
        updateScene();
    }
    void setActiveRowFor(MicRowUI* rw) { const int i = indexOf(rw); if (i >= 0 && i != activeRow) setActiveRow(i); }

    void configureSliderFor(MicRowUI* rw) {
        const auto loc = rw->location.getText();
        if (loc == "room") {                                           // room mic: 15-200 cm, log-ish
            rw->slider.setSliderStyle(juce::Slider::LinearHorizontal);
            rw->slider.setRange(15.0, 200.0, 1.0); rw->slider.setSkewFactorFromMidPoint(60.0);
        } else if (loc == "rear") {                                    // rear mic: 10-100 cm
            rw->slider.setSliderStyle(juce::Slider::LinearVertical);
            rw->slider.setRange(10.0, 100.0, 1.0); rw->slider.setSkewFactorFromMidPoint(30.0);
        }
        rw->slider.setVisible(loc != "grille");
    }
    void placeGrille(MicRowUI* rw) {                                   // put a grille mic on the first free grid spot
        if (const auto spot = ocap::micset::placeGrille(rowSpecs(), indexOf(rw))) {
            rw->position = juce::String(spot->position);
            rw->dist.setText(juce::String((int)spot->distCm), false);
        }
    }
    void locationChangedFor(MicRowUI* rw) {
        const auto loc = rw->location.getText();
        if (countLoc(loc, indexOf(rw)) >= locLimit(loc)) {             // limits: 4 grille / 2 room / 2 rear
            takeTab.status.setText("Limit reached: max " + juce::String(locLimit(loc)) + " " + loc + " mics.",
                           juce::dontSendNotification);
            rw->location.setSelectedId(rw->lastLocId, juce::dontSendNotification);
            return;
        }
        rw->lastLocId = rw->location.getSelectedId();
        rw->position = {};
        rw->dist.setText("", false);                                   // mode switch clears distance (no silent defaults)
        rw->glyph.set(loc);
        configureSliderFor(rw);
        if (loc == "grille") placeGrille(rw);                          // back to grille → re-appear on a free grid spot
        if (loc == "rear" && exportTab.cab.backBox.getText() == "closed")
            takeTab.status.setText("Note: closed-back cab - a rear mic hears the panel, not the speaker."
                           "\nIf you blend it with a front mic later, invert its polarity.", juce::dontSendNotification);
        updateScene();
    }
    void addMicRow() {
        if ((int)takeTab.micRows.size() >= kMaxMics) return;
        const juce::String loc(ocap::micset::newRowLocation(rowSpecs()));  // first free location
        if (loc.isEmpty()) return;
        auto r = std::make_unique<MicRowUI>();
        auto* rw = r.get();
        rw->slot = juce::jmax(0, ocap::micset::firstFreeSlot(rowSpecs(), kMaxMics));
        const auto col = kSlotColours[rw->slot];
        rw->glyph.colour = col;
        rw->hist.colour = col;
        rw->glyph.onClick = [this, rw] { setActiveRowFor(rw); };
        for (int i = 0; i < vocab::location.size(); ++i) rw->location.addItem(vocab::location[i], i + 1);
        rw->location.setSelectedId(loc == "room" ? 2 : loc == "rear" ? 3 : 1, juce::dontSendNotification);
        rw->lastLocId = rw->location.getSelectedId();
        rw->glyph.set(loc);
        rw->location.onChange = [this, rw] { setActiveRowFor(rw); locationChangedFor(rw); };
        if (loc == "grille") placeGrille(rw);                          // start on a FREE grid spot — visible, draggable
        rw->mic.setTextWhenNothingSelected("select mic");
        refreshMicCombo(rw->mic);
        rw->hist.onClick = [this, rw] { setActiveRowFor(rw); };        // clicking the level history selects it too
        rw->tint.frameStyle = true;                                   // slot-colour frame round the whole block
        rw->tint.setInterceptsMouseClicks(true, false);
        rw->tint.onClick = [this, rw] { setActiveRowFor(rw); };
        rw->mic.onChange = [this, rw] {
            setActiveRowFor(rw); refreshCaptureGate();
        };
        rw->addMic.setButtonText("+");
        rw->addMic.setTooltip("Add a mic model to the list.");
        rw->addMic.onClick = [this, rw] { setActiveRowFor(rw); addModelPrompt(rw); };
        for (int i = 0; i < vocab::axis.size(); ++i) rw->axis.addItem(vocab::axis[i], i + 1);
        rw->axis.setTextWhenNothingSelected("select axis");
        rw->axis.setSelectedId(1, juce::dontSendNotification);         // On-axis — the conscious default
        rw->axis.onChange = [this, rw] { setActiveRowFor(rw); };
        rw->dist.setTextToShowWhenEmpty("dist", juce::Colours::grey);
        rw->dist.setInputRestrictions(6, "0123456789.");
        rw->dist.onTextChange = [this, rw] {
            setActiveRowFor(rw);
            const double v = rw->dist.getText().getDoubleValue();
            rw->slider.setValue(distIsInches() ? v * 2.54 : v, juce::dontSendNotification);
            updateScene();
        };
        rw->input.setTextWhenNothingSelected("in?");
        for (int i = 0; i < inNames.size(); ++i) rw->input.addItem(juce::String(i + 1) + ": " + inNames[i], i + 1);
        { const int free = lowestFreeInput();                          // grab the next unused input (mic1 -> in1)
          if (free >= 0) rw->input.setSelectedId(free + 1, juce::dontSendNotification); }
        rw->input.onChange = [this, rw] { setActiveRowFor(rw); syncActiveChannel(); updateScene(); };
        rw->slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        rw->slider.setColour(juce::Slider::thumbColourId, col);
        rw->slider.setColour(juce::Slider::trackColourId, col.darker(0.5f));
        rw->slider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff262b32));
        rw->slider.onValueChange = [this, rw] {
            setActiveRowFor(rw);
            const double cm = rw->slider.getValue();
            const double v = distIsInches() ? cm / 2.54 : cm;
            rw->dist.setText(juce::String(v, (v == (double)(int)v) ? 0 : 1), false);
            updateScene();
        };
        rw->removeBtn.setButtonText("x");
        rw->removeBtn.onClick = [this, rw] {
            const int i = indexOf(rw);
            if (i < 0 || takeTab.micRows.size() <= 1) return;
            juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Remove mic",
                "Remove mic" + juce::String(i + 1) + " (" + (rw->mic.getText().isNotEmpty() ? rw->mic.getText() : juce::String("unset"))
                + ") from the setup?", "Remove", "Cancel", this,
                juce::ModalCallbackFunction::create([this, rw](int ok) {
                    if (ok == 1) { const int j = indexOf(rw); if (j >= 0) removeMicRow(j); }
                }));
        };
        juce::Component* holderCs[] = { &rw->tint, &rw->location, &rw->mic, &rw->addMic, &rw->axis,
                                        &rw->dist, &rw->input, &rw->removeBtn, &rw->hist };
        for (auto* c : holderCs) takeTab.micHolder.addAndMakeVisible(c);
        takeTab.addAndMakeVisible(rw->slider);                        // the room/rear slider lives in the scene panel
        takeTab.micRows.push_back(std::move(r));
        configureSliderFor(rw);
        setActiveRow((int)takeTab.micRows.size() - 1);
        refreshCaptureGate();
    }
    void removeMicRow(int i) {
        if ((int)takeTab.micRows.size() <= 1) return;                          // every take has at least one mic
        takeTab.micRows.erase(takeTab.micRows.begin() + i);
        setActiveRow(juce::jmin(activeRow, (int)takeTab.micRows.size() - 1));
        refreshCaptureGate();
    }
    // Rebuild everything the mic set projects onto: grid dots, row tints, sliders.
    void updateScene() {
        if (takeTab.micRows.empty()) return;
        takeTab.grid.setUnit(distUnitText());
        const bool inches = distIsInches();
        std::vector<MicGrid::Dot> ds;
        for (int i = 0; i < (int)takeTab.micRows.size(); ++i) {
            auto& r = *takeTab.micRows[(size_t)i];
            r.tint.colour = kSlotColours[r.slot];
            r.tint.active = (i == activeRow);
            r.tint.repaint();
            r.removeBtn.setEnabled(takeTab.micRows.size() > 1);
            if (r.location.getText() == "grille" && r.position.isNotEmpty()) {
                const double mm = rowDistanceMm(r);
                ds.push_back({ i, r.position, inches ? mm / 25.4 : mm / 10.0, kSlotColours[r.slot], i == activeRow });
            }
        }
        takeTab.grid.setDots(std::move(ds));
        takeTab.addMicBtn.setEnabled((int)takeTab.micRows.size() < kMaxMics);
        takeTab.resized();
    }


    // A red outline flags a mic with no model. Capture itself stays clickable — pressing it with
    // problems opens a copyable list (see captureProblems / showProblemsDialog) instead of blocking.
    void refreshCaptureGate() {
        for (auto& rp : takeTab.micRows) {
            const bool has = rp->mic.getText().trim().isNotEmpty();
            rp->mic.setColour(juce::ComboBox::outlineColourId,
                              has ? juce::Colour(0xff3a3f47) : juce::Colours::red.withAlpha(0.95f));
            rp->mic.repaint();
        }
    }
    // Everything that would make this capture unusable / incomplete, one line each (copyable).
    juce::StringArray captureProblems() const {
        juce::StringArray p;
        for (int i = 0; i < (int)takeTab.micRows.size(); ++i) {
            const auto& r = *takeTab.micRows[(size_t)i];
            if (r.mic.getText().trim().isEmpty()) p.add("mic" + juce::String(i + 1) + ": no model selected");
            if (r.input.getSelectedId() == 0)     p.add("mic" + juce::String(i + 1) + ": no input assigned");
        }
        for (int i = 0; i < (int)takeTab.micRows.size(); ++i)
            for (int j = i + 1; j < (int)takeTab.micRows.size(); ++j)
                if (takeTab.micRows[(size_t)i]->input.getSelectedId() != 0 &&
                    takeTab.micRows[(size_t)i]->input.getSelectedId() == takeTab.micRows[(size_t)j]->input.getSelectedId())
                    p.add("mic" + juce::String(i + 1) + " and mic" + juce::String(j + 1)
                          + " share input (" + takeTab.micRows[(size_t)i]->input.getText() + ") - give each its own channel");
        return p;
    }
    // A custom modal (NOT AlertWindow — it won't size a multi-line editor) so ALL problems show,
    // fully, in a read-only + copyable list.
    void showProblemsDialog(const juce::StringArray& problems) {
        struct Panel : juce::Component {
            juce::Label head; juce::TextEditor ed; juce::TextButton copyBtn { "Copy" }, okBtn { "OK" };
            juce::String txt;
            explicit Panel(const juce::String& t) : txt(t) {
                head.setText("Can't capture yet - fix these:", juce::dontSendNotification);
                head.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
                head.setColour(juce::Label::textColourId, juce::Colours::white);
                ed.setMultiLine(true, true); ed.setReadOnly(true); ed.setCaretVisible(false);
                ed.setScrollbarsShown(true); ed.setJustification(juce::Justification::topLeft);
                ed.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff2b2f36));
                ed.setColour(juce::TextEditor::textColourId, juce::Colours::white);
                ed.setColour(juce::TextEditor::outlineColourId, juce::Colours::grey.withAlpha(0.4f));
                ed.setText(t, false);
                copyBtn.onClick = [this] { juce::SystemClipboard::copyTextToClipboard(txt); };
                okBtn.onClick   = [this] { if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState(0); };
                for (juce::Component* c : { (juce::Component*)&head, (juce::Component*)&ed,
                                           (juce::Component*)&copyBtn, (juce::Component*)&okBtn })
                    addAndMakeVisible(c);
            }
            void resized() override {
                auto r = getLocalBounds().reduced(14);
                head.setBounds(r.removeFromTop(22)); r.removeFromTop(8);
                auto btns = r.removeFromBottom(30); r.removeFromBottom(10);
                okBtn.setBounds(btns.removeFromRight(84)); btns.removeFromRight(8);
                copyBtn.setBounds(btns.removeFromRight(84));
                ed.setBounds(r);
            }
        };
        auto* panel = new Panel(problems.joinIntoString("\n"));
        panel->setSize(480, juce::jlimit(150, 480, 96 + 22 * problems.size()));
        juce::DialogWindow::LaunchOptions opt;
        opt.content.setOwned(panel);
        opt.dialogTitle = "OrbitCapture";
        opt.dialogBackgroundColour = juce::Colour(0xff23252b);
        opt.escapeKeyTriggersCloseButton = true;
        opt.useNativeTitleBar = false;
        opt.resizable = false;
        opt.launchAsync();
    }

    // Global cm/in switch: convert every row's displayed distance from the old unit to the new.
    void onDistUnitChanged() {
        const bool nowIn = distIsInches();
        if (nowIn != prevInches) {
            for (auto& rp : takeTab.micRows) {
                if (rp->dist.getText().isEmpty()) continue;
                const double old = rp->dist.getText().getDoubleValue();
                const double mm  = prevInches ? old * 25.4 : old * 10.0;
                const double nv  = nowIn ? mm / 25.4 : mm / 10.0;
                rp->dist.setText(juce::String(nv, (nv == (double)(int)nv) ? 0 : 1), false);
            }
            prevInches = nowIn;
        }
        takeTab.grid.setUnit(distUnitText());
        updateScene();
    }

    // [+] next to a mic picker: prompt for a model, add it to the shared list (deduped), select it.
    void addModelPrompt(MicRowUI* rw) {
        textPrompt("Add mic model", "", [this, rw](juce::String v) {
            v = v.trim();
            if (v.isEmpty()) return;
            const bool existed = listStore.get("mic").contains(v, true);
            listStore.add("mic", v);                                   // add() is dedup-safe (ignores an existing value)
            for (auto& m : takeTab.micRows)                                    // rebuild every picker; select v in this row
                refreshMicCombo(m->mic, (m.get() == rw) ? v : m->mic.getText());
            refreshCaptureGate();
            if (existed)
                takeTab.status.setText("\"" + v + "\" is already in the list - selected it.", juce::dontSendNotification);
        });
    }
    // Gear-list management ([...] menus) lives in ui/tabs/CabForm.h; the mic-model picker below
    // stays here (it feeds the mic rows + the edit-mics dialog).
    // Mic picker: Factory group (vocab set) then Added group (user), each sorted A-Z, headed + separated.
    void refreshMicCombo(juce::ComboBox& c, const juce::String& sel = {}) {
        const juce::String keep = sel.isNotEmpty() ? sel : c.getText();
        c.clear(juce::dontSendNotification);
        juce::StringArray factory, user;
        for (const auto& s : listStore.get("mic")) (vocab::mics.contains(s, true) ? factory : user).add(s);
        factory.sortNatural(); user.sortNatural();
        int id = 1, selId = 0;
        auto addGroup = [&](const juce::StringArray& grp) {
            for (const auto& s : grp) { c.addItem(s, id); if (s == keep) selId = id; ++id; }
        };
        c.addSectionHeading("Factory"); addGroup(factory);
        if (!user.isEmpty()) { c.addSeparator(); c.addSectionHeading("Added"); addGroup(user); }
        if (selId > 0) c.setSelectedId(selId, juce::dontSendNotification);
        else if (keep.isNotEmpty()) c.setText(keep, juce::dontSendNotification);
    }

    // ---- the sample recorder: capture the dry live input into a persisted user sample ----
    void startSampleRecording() {
        if (engine.conv.mode.load() != 2) {                            // recording implies live monitoring
            reviewTab.playLiveBtn.setToggleState(true, juce::dontSendNotification);
            engine.audition.stop(); reloadLiveIR(); engine.conv.setMode(2);
        }
        engine.rec.start();
        reviewTab.reviewInfo.setText("REC - play; press Rec again to save (up to 60 s).", juce::dontSendNotification);
    }
    void finishSampleRecording() {
        engine.rec.stop();
        reviewTab.recBtn.setToggleState(false, juce::dontSendNotification);
        const int n = engine.rec.len.load();
        if (n < (int)(engine.sampleRate / 4)) {                        // under 250 ms: nothing worth keeping
            reviewTab.reviewInfo.setText("Recording too short - nothing saved.", juce::dontSendNotification);
            return;
        }
        std::vector<float> clip(engine.rec.buf.begin(), engine.rec.buf.begin() + n);
        {   // trim the silence around the phrase (keep a little air before the onset)
            const auto on = ocap::irimport::onsetIndex(clip);
            float pk = 0.0f; for (float v : clip) pk = std::max(pk, std::abs(v));
            int endI = (int)clip.size() - 1;
            while (endI > 0 && std::abs(clip[(size_t)endI]) < 0.02f * pk) --endI;
            const int startI = (int)std::max<std::ptrdiff_t>(0, on - (std::ptrdiff_t)(engine.sampleRate * 0.05));
            clip.assign(clip.begin() + startI, clip.begin() + std::min((int)clip.size(), endI + (int)(engine.sampleRate * 0.25)));
        }
        const double sr = engine.sampleRate;
        textPrompt("Sample name", "riff " + juce::Time::getCurrentTime().formatted("%H-%M"),
                   [this, clip = std::move(clip), sr](juce::String nm) {
            const juce::String base(ocap::exportplan::sanitizeName(nm.toStdString()));
            auto f = samplesDir().getChildFile(base + ".wav");
            int c = 2;
            while (f.existsAsFile()) f = samplesDir().getChildFile(base + " " + juce::String(c++) + ".wav");
            oc::wav_write_mono_f32(f.getFullPathName().toStdString(),
                                   std::vector<double>(clip.begin(), clip.end()), sr);
            DiClip dc; dc.name = f.getFileNameWithoutExtension(); dc.sr = sr; dc.samples = clip;
            userClips.push_back(std::move(dc));
            rebuildDiBox(userClips.back().name);
            reviewTab.reviewInfo.setText("Saved \"" + f.getFileNameWithoutExtension()
                                       + "\" (" + juce::String((double)clip.size() / sr, 1) + " s) to your samples.",
                                         juce::dontSendNotification);
        });
    }
    void deleteSelectedUserSample() {
        const int idx = reviewTab.diBox.getSelectedId() - 1;           // ids are sequential: user first
        if (idx < 0 || idx >= (int)userClips.size()) {
            reviewTab.reviewInfo.setText("Factory samples can't be deleted - pick one of your own (User group).",
                                         juce::dontSendNotification);
            return;
        }
        const juce::String nm = userClips[(size_t)idx].name;
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Delete sample",
            "Delete \"" + nm + "\" from your samples? (the file is removed)", "Delete", "Cancel", this,
            juce::ModalCallbackFunction::create([this, idx, nm](int okPressed) {
                if (okPressed != 1) return;
                for (const auto& f : samplesDir().findChildFiles(juce::File::findFiles, false))
                    if (f.getFileNameWithoutExtension() == nm) f.deleteFile();
                userClips.erase(userClips.begin() + idx);
                rebuildDiBox({});
                reviewTab.reviewInfo.setText("Deleted \"" + nm + "\".", juce::dontSendNotification);
            }));
    }

    // ---- DI sample library: factory riffs + the user's own clips (persisted in app-data) ----
    struct DiClip { juce::String name; std::vector<float> samples; double sr = 48000.0; };
    static juce::File samplesDir() {
        auto d = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                     .getChildFile("OrbitCapture").getChildFile("samples");
        d.createDirectory();
        return d;
    }
    void scanUserSamples() {
        for (const auto& f : samplesDir().findChildFiles(juce::File::findFiles, false)) {
            std::unique_ptr<juce::AudioFormatReader> rd(diFormats.createReaderFor(f));
            if (rd == nullptr || rd->lengthInSamples <= 0 || rd->sampleRate <= 0) continue;
            const int n = (int)std::min<juce::int64>(rd->lengthInSamples, (juce::int64)(rd->sampleRate * 60.0));
            juce::AudioBuffer<float> b((int)rd->numChannels, n);
            rd->read(&b, 0, n, 0, true, true);
            DiClip c; c.name = f.getFileNameWithoutExtension(); c.sr = rd->sampleRate;
            c.samples.assign(b.getReadPointer(0), b.getReadPointer(0) + n);
            if (b.getNumChannels() > 1)
                for (int i = 0; i < n; ++i) c.samples[(size_t)i] = 0.5f * (b.getReadPointer(0)[i] + b.getReadPointer(1)[i]);
            userClips.push_back(std::move(c));
        }
    }
    // User clips first (their session, their sound), then the factory set — grouped + headed.
    // Combo ids stay sequential across the groups so id-1 indexes allClips().
    std::vector<const DiClip*> allClips() const {
        std::vector<const DiClip*> v;
        for (const auto& c : userClips)    v.push_back(&c);
        for (const auto& c : factoryClips) v.push_back(&c);
        return v;
    }
    void rebuildDiBox(const juce::String& select) {
        auto& box = reviewTab.diBox;
        const juce::String keep = select.isNotEmpty() ? select : box.getText();
        box.clear(juce::dontSendNotification);
        int id = 1, selId = 0;
        auto addGroup = [&](const char* head, const std::vector<DiClip>& grp) {
            if (grp.empty()) return;
            box.addSectionHeading(head);
            for (const auto& c : grp) { box.addItem(c.name, id); if (c.name == keep) selId = id; ++id; }
        };
        addGroup("User", userClips);
        addGroup("Factory", factoryClips);
        box.setSelectedId(selId > 0 ? selId : 1, juce::dontSendNotification);
    }

    // Render a DI riff (dry, or convolved through the last captured IR) and hand it to the
    // audio thread. Runs on the message thread; the swap discipline (stop → assign within
    // reserved capacity → start) is owned by the stream types in audio/RtStreams.h.
    std::vector<float> resampledDI() const {                          // selected DI clip at the device rate
        const auto clips = allClips();
        if (clips.empty()) return {};
        const int idx = juce::jlimit(0, (int)clips.size() - 1, reviewTab.diBox.getSelectedId() - 1);
        const auto& clip = *clips[(size_t)idx];
        if (clip.samples.empty()) return {};
        if (std::abs(engine.sampleRate - clip.sr) < 1.0) return clip.samples;
        const int outN = juce::jmax(1, (int)((double)clip.samples.size() * engine.sampleRate / clip.sr));
        std::vector<float> di((size_t)outN, 0.0f);
        juce::LagrangeInterpolator interp;
        interp.process(clip.sr / engine.sampleRate, clip.samples.data(), di.data(), outN);
        return di;
    }
    // The wet monitoring gain: loudness-match against the ACTUAL selected sample — RMS of the
    // clip dry vs through the mix (first ~2 s). Band-blind norms (peak, whole-band energy) kept
    // failing because guitar energy sits exactly under the cab's hump. Falls back to unit-energy
    // when no clip is loaded (live-input-only monitoring).
    float monitorGainFor(const std::vector<float>& ir) const {
        double e = 0.0;
        for (float v : ir) e += (double)v * v;
        float g = e > 0.0 ? (float)(1.0 / std::sqrt(e)) : 1.0f;
        const auto di = resampledDI();
        const size_t n = std::min(di.size(), (size_t)(engine.sampleRate * 2.0));
        if (n > 256) {
            const std::vector<float> seg(di.begin(), di.begin() + (std::ptrdiff_t)n);
            const auto wet = ocap::convolveDI(seg, ir);
            double rd = 0, rw = 0;
            for (float v : seg) rd += (double)v * v;
            for (float v : wet) rw += (double)v * v;
            if (rd > 0.0 && rw > 0.0) g = (float)std::sqrt(rd / rw);
        }
        return g;
    }
    void reloadLiveIR() {                                             // push the current output IR into the live convolver
        const bool dry = reviewTab.bypassBtn.getToggleState();
        std::vector<float> ir = dry ? std::vector<float>{ 1.0f }      // bypass = the untouched reference:
                                    : currentOutputIR();              // unity, independent of ALL mix controls
        if (ir.empty()) return;
        if (!dry) {
            // Wet: loudness-matched to the dry sample, then the Master fader on top as the mix's
            // own (deliberate) level offset. Master never touches the bypass side.
            const float master = (float)std::pow(10.0, (reviewTab.masterStrip ? gatherMaster().gainDb : 0.0) / 20.0);
            const float g = monitorGainFor(ir) * master;              // currentOutputIR had master inside; the
            for (float& v : ir) v *= g;                               // match wiped it — re-applied once here
        }
        juce::AudioBuffer<float> buf(1, (int)ir.size());
        std::copy(ir.begin(), ir.end(), buf.getWritePointer(0));
        engine.liveConv.loadImpulseResponse(std::move(buf), lastIRSr,
                                     juce::dsp::Convolution::Stereo::no,
                                     juce::dsp::Convolution::Trim::no,
                                     juce::dsp::Convolution::Normalise::no);
    }
    // Stream the DI clip through the live convolver (wet audition) — knob turns are heard immediately.
    void startWetAudition() {
        const auto di = resampledDI();
        if (di.empty()) { reviewTab.reviewInfo.setText("Pick or load a DI clip first.", juce::dontSendNotification); return; }
        if (currentOutputIR().empty()) return;
        engine.conv.stop();                                                   // pause the stream before refilling the buffer
        engine.conv.refill(di.data(), di.size());                             // asserts mode-down + reserved capacity
        engine.audition.stop();
        reviewTab.playLiveBtn.setToggleState(false, juce::dontSendNotification);
        reloadLiveIR();
        refreshSpectrum();
        engine.conv.setMode(1);
        reviewTab.reviewInfo.setText("Playing the mix - turn the mixer knobs to hear changes live.", juce::dontSendNotification);
    }
    void playRender(std::vector<float> render, const juce::String& what) {
        float pk = 0.0f; for (float v : render) pk = std::max(pk, std::abs(v));
        const float gg = pk > 0.0f ? 0.72f / pk : 1.0f;
        for (auto& v : render) v *= gg;
        engine.audition.publish(render.data(), render.size());               // stop → assign within capacity → start
        reviewTab.reviewInfo.setText("Playing " + what + "  -  " + reviewTab.diBox.getText(), juce::dontSendNotification);
    }
    void renderAudition(bool wet) {
        auto di = resampledDI();
        if (di.empty()) { reviewTab.reviewInfo.setText("DI clip didn't load.", juce::dontSendNotification); return; }
        if (!wet) { playRender(std::move(di), "dry DI"); return; }
        if (lastIRs.empty()) { reviewTab.reviewInfo.setText("Capture a cab first (Capture tab), then audition.", juce::dontSendNotification); return; }
        const auto ir = currentOutputIR();                            // the full mix through the Master bus
        if (ir.empty()) return;
        refreshSpectrum();                                            // keep the picture in sync with the sound
        playRender(ocap::convolveDI(di, ir), reviewTab.mixRows.size() >= 2 ? "the mix" : "DI through the cab");
    }

    // ---- blend mixer (only for multi-mic sets): gains + polarity + HPF/LPF, live spectrum overlay ----
    static int slopeOf(const juce::ComboBox& c) {                     // combo text -> dB/oct, 0 = off
        const auto t = c.getText();
        return (t.isEmpty() || t == "off") ? 0 : t.getIntValue();
    }
    static void selectSlope(juce::ComboBox& c, int dbOct) {           // pick the item matching a dB/oct
        for (int i = 0; i < c.getNumItems(); ++i)
            if (c.getItemText(i).getIntValue() == dbOct)
                { c.setSelectedId(c.getItemId(i), juce::dontSendNotification); return; }
        c.setSelectedId(1, juce::dontSendNotification);
    }
    // The IR Review shows/plays: the full mix through the Master bus (multi-mic), or the raw mic
    // for a single-mic capture (no mixer).
    std::vector<float> currentOutputIR() const {
        if (lastIRs.empty()) return {};
        if (reviewTab.mixRows.size() == lastIRs.size()) return computeBlend();   // 1 channel = a 1-strip console
        return lastIRs[0];
    }
    // Redraw the frequency-response view. Multi-mic: the mix overlay + the ACTIVE strip's draggable HPF/LPF lines
    // (tinted its colour). Single-mic: just the raw curve, no draggable lines.
    void refreshSpectrum() {
        if (lastIRs.empty()) return;
        renderBlendOverlay();
        const MixStrip* a = activeStrip ? activeStrip : reviewTab.masterStrip.get();
        if (a) reviewTab.spectrumView.setActiveFilter(true, a->hpBtn.getToggleState(), a->lpBtn.getToggleState(),
                                            a->hpfHz, a->hpfSlopeDb, a->lpfHz, a->lpfSlopeDb, a->colour);
    }
    // Highlight the clicked strip and aim the graph's draggable filter lines at its HPF/LPF.
    void setActiveStrip(MixStrip* s) {
        activeStrip = s;
        for (auto& mp : reviewTab.mixRows) mp->setActive(mp.get() == s);
        if (reviewTab.masterStrip) reviewTab.masterStrip->setActive(reviewTab.masterStrip.get() == s);
        updateLegend();
        refreshSpectrum();
    }
    // The graph's colour legend: one row per mic (full description — the strips only carry the
    // short token) + the MIX row; the active strip's row is highlighted. Click a row → its strip.
    void updateLegend() {
        std::vector<SpectrumView::LegendEntry> le;
        if (!reviewTab.mixRows.empty()) {
            const auto strips = gatherStrips();
            for (int m = 0; m < (int)reviewTab.mixRows.size() && m < lastIRNames.size(); ++m) {
                const auto desc = lastIRFileBases[m].fromFirstOccurrenceOf(" - ", false, false);
                le.push_back({ lastIRNames[m] + (desc.isNotEmpty() ? "   " + desc : juce::String()),
                               lastIRColours[(size_t)m], !ocap::channelAudible(strips, (size_t)m) });
            }
            le.push_back({ "MIX", juce::Colours::white, false });
        }
        int act = (int)le.size() - 1;                                  // default highlight: the MIX row
        for (int m = 0; m < (int)reviewTab.mixRows.size(); ++m)
            if (activeStrip == reviewTab.mixRows[(size_t)m].get()) act = m;
        const bool empty = le.empty();
        reviewTab.spectrumView.setLegend(std::move(le), empty ? -1 : act);
    }
    std::vector<ocap::StripParams> gatherStrips() const {
        std::vector<ocap::StripParams> v; v.reserve(reviewTab.mixRows.size());
        for (const auto& s : reviewTab.mixRows) v.push_back(s->params());
        return v;
    }
    ocap::MasterParams gatherMaster() const {
        return reviewTab.masterStrip ? reviewTab.masterStrip->masterParams() : ocap::MasterParams{};
    }
    std::vector<float> computeBlend(bool applyMaster = true) const {
        if (lastIRs.empty() || reviewTab.mixRows.size() != lastIRs.size()) return {};
        return ocap::blendIrs(lastIRs, gatherStrips(), gatherMaster(), lastIRSr, applyMaster);
    }
    // Configure every control on a strip (channel or Master) + wire its callbacks. Any interaction
    // makes the strip the active one (so its HPF/LPF become the graph's draggable filter lines).
    void wireStrip(MixStrip& s) {
        const auto col = s.colour;
        MixStrip* sp = &s;
        auto live   = [this] { refreshSpectrum(); if (engine.conv.mode.load() != 0) reloadLiveIR(); };
        auto select = [this, sp] { setActiveStrip(sp); };
        if (s.isMaster) s.name.setColour(juce::Label::textColourId, juce::Colours::white);
        s.gain.setRange(-24.0, 6.0, 0.1);                             // fader style/textbox live in MixStrip (the view)
        s.gain.setValue(0.0, juce::dontSendNotification);
        s.gain.setTextValueSuffix(" dB");
        s.gain.setColour(juce::Slider::thumbColourId, col);
        s.gain.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);   // no frame round the dB value
        s.gain.setDoubleClickReturnValue(true, 0.0);                  // double-click → 0 dB
        s.gain.setTooltip(s.isMaster ? "MONITOR level (audition / live) - the exported MIX is peak-normalized,\n"
                                       "so this never changes the files. Pull it down when many mics clip,\n"
                                       "or level-match against bypass. Double-click = 0 dB."
                                     : "Mic level in the mix (the balance IS the sound). Double-click = 0 dB.");
        s.gain.onValueChange = live; s.gain.onDragStart = select; s.gain.onDragEnd = [this] { saveMixToTake(); };
        if (!s.isMaster) {
            auto cfgKnob = [this, col](EditKnob& sl) {                    // rotary KNOB, value drawn INSIDE; single-click = type
                sl.setSliderStyle(juce::Slider::RotaryVerticalDrag);
                sl.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                sl.setColour(juce::Slider::rotarySliderFillColourId, col);
                sl.setScrollWheelEnabled(true);                          // wheel adjusts the knob
                sl.setLookAndFeel(&knobLnf);
                sl.onCommit = [this] { saveMixToTake(); };               // a typed value belongs to the take
            };
            cfgKnob(s.phase);                                            // phase = all-pass rotation, -180..+180 deg
            s.phase.setRange(-180.0, 180.0, 1.0);
            s.phase.setValue(0.0, juce::dontSendNotification);
            s.phase.textFromValueFunction = [](double vv) { const int d = (int)std::lround(vv);
                return (d > 0 ? juce::String("+") : juce::String()) + juce::String(d) + juce::String::fromUTF8("\xc2\xb0"); };
            s.phase.valueFromTextFunction = [](const juce::String& t) { return t.retainCharacters("-0123456789").getDoubleValue(); };
            s.phase.setTooltip("Phase rotation (all-pass); +/-180 = polarity flip. Frequency-independent (not a delay).");
            s.phase.updateText();
            s.phase.onValueChange = live; s.phase.onDragStart = select; s.phase.onDragEnd = [this] { saveMixToTake(); };
            cfgKnob(s.shift);                                            // bipolar time-shift KNOB, 0 = off
            s.shift.setRange(-kShiftMs, kShiftMs, 0.01);
            s.shift.setValue(0.0, juce::dontSendNotification);
            s.shift.textFromValueFunction = [](double vv) {
                if (std::abs(vv) < 0.005) return juce::String("0 ms");
                return juce::String(vv > 0.0 ? "+" : "") + juce::String(vv, 2) + " ms"; };
            s.shift.valueFromTextFunction = [](const juce::String& t) {
                const auto q = t.retainCharacters("-0123456789.");
                return q.isEmpty() ? 0.0 : juce::jlimit(-kShiftMs, kShiftMs, q.getDoubleValue()); };
            s.shift.setTooltip("Time-shift (ms) — aligns this mic against the others. A pure delay (combs, doesn't EQ a lone mic).");
            s.shift.updateText();
            s.shift.onValueChange = live; s.shift.onDragStart = select; s.shift.onDragEnd = [this] { saveMixToTake(); };
            s.solo.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffffd54f));
            s.solo.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
            s.solo.setTooltip("Solo — hear only the soloed mic(s).");
            s.mute.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffe57373));
            s.mute.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
            s.mute.setTooltip("Mute this mic.");
            s.solo.onClick = [this, sp] { setActiveStrip(sp); refreshSpectrum(); if (engine.conv.mode.load() != 0) reloadLiveIR(); saveMixToTake(); };
            s.mute.onClick = [this, sp] { setActiveStrip(sp); refreshSpectrum(); if (engine.conv.mode.load() != 0) reloadLiveIR(); saveMixToTake(); };
        }
        // HPF / LPF each enable a side of the strip's filter (default 80 Hz·24 / 8 kHz·12 — benign on an
        // accidental toggle); the cutoff is dragged on the graph (up/down = slope), the wheel steps the slope.
        const juce::String what = s.isMaster ? "mix" : "mic";
        for (auto* fb : { &s.hpBtn, &s.lpBtn }) {
            fb->setColour(juce::TextButton::buttonOnColourId, col.withAlpha(0.9f));
            fb->setColour(juce::TextButton::textColourOnId, juce::Colours::black);
            fb->onClick = [this, sp] { setActiveStrip(sp); refreshSpectrum();
                                       if (engine.conv.mode.load() != 0) reloadLiveIR(); saveMixToTake(); };
        }
        s.hpBtn.setTooltip("High-pass this " + what + " (default 80 Hz, 24 dB/oct). Drag its line on the graph: left/right = cutoff, up/down = slope; wheel = slope; double-click = reset.");
        s.lpBtn.setTooltip("Low-pass this " + what + " (default 8 kHz, 12 dB/oct). Drag its line on the graph: left/right = cutoff, up/down = slope; wheel = slope; double-click = reset.");
        if (s.isMaster) {                                             // Reset + Auto live on the Master strip
            s.reset.setTooltip("Flat mixer: 0 dB, no phase/shift, all filters off, no solo/mute.");
            s.reset.onClick = [this] { confirmResetMixer(); };
            s.autoBtn.setTooltip("Auto-align: time-shift (\xc2\xb1" + juce::String(kShiftMs, 0)
                                 + " ms) + polarity of every channel against a reference - kills comb filtering. "
                                   "Reference: the selected channel strip, otherwise the loudest channel. "
                                   "You don't need to know which mic stood where - only mutual alignment is audible.");
            s.autoBtn.onClick = [this] { autoAlignMixer(); };
        }
        s.onSelect = select;
    }
    // "Auto" on the Master strip: cross-correlate every channel against a reference and set the
    // shift knobs (+ polarity via phase 180) so the set sums coherently (core/AutoAlign.h).
    // The reference needs no knowledge of the rig: the SELECTED channel strip if there is one,
    // otherwise the loudest channel (usually the close mic — the most confident anchor). For
    // comb-killing the choice barely matters: only MUTUAL alignment is audible.
    void autoAlignMixer() {
        if (reviewTab.mixRows.size() < 2 || lastIRs.size() != reviewTab.mixRows.size()) return;
        size_t ref = 0; bool selected = false;
        for (size_t m = 0; m < reviewTab.mixRows.size(); ++m)          // the selected strip wins...
            if (activeStrip == reviewTab.mixRows[m].get()) { ref = m; selected = true; }
        if (!selected) {                                               // ...else the loudest channel
            double best = -1.0;
            for (size_t m = 0; m < lastIRs.size(); ++m) {
                double e = 0; for (float v : lastIRs[m]) e += (double)v * v;
                if (e > best) { best = e; ref = m; }
            }
        }
        const auto al = ocap::autoalign::align(lastIRs, lastIRSr, ref, kShiftMs);
        juce::String msg = "Auto-aligned to " + reviewTab.mixRows[ref]->name.getText()
                         + (selected ? " (selected):" : " (loudest):");
        for (size_t m = 0; m < al.size(); ++m) {
            auto& s = *reviewTab.mixRows[m];
            s.shift.setValue(al[m].shiftMs, juce::dontSendNotification); s.shift.updateText();
            if (m == ref) continue;                                    // the reference keeps its phase seasoning
            s.phase.setValue(al[m].invert ? 180.0 : 0.0, juce::dontSendNotification); s.phase.updateText();
            msg << "  " << s.name.getText() << " " << (al[m].shiftMs >= 0 ? "+" : "")
                << juce::String(al[m].shiftMs, 2) << "ms" << (al[m].invert ? " inv" : "");
        }
        updateLegend(); refreshSpectrum(); saveMixToTake();
        reviewTab.reviewInfo.setText(msg, juce::dontSendNotification);
    }
    void confirmResetMixer() {
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Reset mixer",
            "Reset every channel and the Master to flat? (0 dB, no phase/shift, filters off, no solo/mute)",
            "Reset", "Cancel", this,
            juce::ModalCallbackFunction::create([this](int ok) {
                if (ok != 1) return;
                auto flat = [](MixStrip& s) {                          // silent — one render + one save at the end
                    s.gain.setValue(0.0, juce::dontSendNotification);
                    if (!s.isMaster) {
                        s.phase.setValue(0.0, juce::dontSendNotification); s.phase.updateText();
                        s.shift.setValue(0.0, juce::dontSendNotification); s.shift.updateText();
                        s.solo.setToggleState(false, juce::dontSendNotification);
                        s.mute.setToggleState(false, juce::dontSendNotification);
                    }
                    s.hpBtn.setToggleState(false, juce::dontSendNotification);
                    s.lpBtn.setToggleState(false, juce::dontSendNotification);
                    s.hpfHz = 80.0; s.hpfSlopeDb = 24; s.lpfHz = 8000.0; s.lpfSlopeDb = 12;
                };
                for (auto& mr : reviewTab.mixRows) flat(*mr);
                if (reviewTab.masterStrip) flat(*reviewTab.masterStrip);
                updateLegend(); refreshSpectrum(); saveMixToTake();
            }));
    }
    void rebuildMixer() {
        reviewTab.hasCapture = !lastIRs.empty();
        reviewTab.mixRows.clear();
        reviewTab.masterStrip.reset();
        activeStrip = nullptr;
        overlayRefDb = std::numeric_limits<double>::quiet_NaN();       // new take → re-anchor the display reference
        if (!lastIRs.empty()) {
            for (int m = 0; m < (int)lastIRs.size(); ++m) {
                auto s = std::make_unique<MixStrip>(false);
                s->setAccent(lastIRColours[(size_t)m]);
                // Console column: short token on the strip ("C414"), input as the context line;
                // the full description lives in the tooltip + the graph legend (duplicate models
                // are told apart by colour + inN).
                const auto model = lastIRFileBases[m].upToFirstOccurrenceOf(" - ", false, false).trim();
                const int sp = model.lastIndexOfChar(' ');
                s->name.setText(sp >= 0 ? model.substring(sp + 1) : model, juce::dontSendNotification);
                s->sub.setText(lastIRNames[m].fromLastOccurrenceOf("(", false, false)
                                             .upToFirstOccurrenceOf(")", false, false), juce::dontSendNotification);
                const auto desc = lastIRFileBases[m].fromFirstOccurrenceOf(" - ", false, false);
                s->setTooltip(lastIRNames[m] + (desc.isNotEmpty() ? "  -  " + desc : juce::String())
                              + "\n(double-click / right-click: edit; x: delete)");
                s->kill.setVisible(lastIRs.size() > 1);              // the last channel goes with the take, not alone
                s->onEdit   = [this, m] { showChannelEditor(m); };
                s->onDelete = [this, m] { confirmDeleteChannel(m); };
                wireStrip(*s);
                reviewTab.addAndMakeVisible(s.get());
                reviewTab.mixRows.push_back(std::move(s));
            }
            reviewTab.masterStrip = std::make_unique<MixStrip>(true);
            reviewTab.masterStrip->setAccent(juce::Colours::white);
            reviewTab.masterStrip->name.setText("Master", juce::dontSendNotification);
            wireStrip(*reviewTab.masterStrip);
            reviewTab.addAndMakeVisible(reviewTab.masterStrip.get());
            activeStrip = reviewTab.masterStrip.get();
            reviewTab.masterStrip->setActive(true);
        }
        const bool haveMixer = !reviewTab.mixRows.empty();
        reviewTab.addChannelBtn.setVisible(currentTake >= 0 && currentTake < (int)takeDirs.size()
                                           && (int)reviewTab.mixRows.size() < kMaxMics);
        updateLegend();
        reviewTab.resized();
        if (haveMixer) renderBlendOverlay();
        else {                                                         // an empty take shows an empty graph —
            reviewTab.spectrumView.setIR({}, 0.0);                     // stale curves would be the PREVIOUS take's
            reviewTab.spectrumView.setActiveFilter(false, false, false, 80.0, 24, 8000.0, 12, brand::lilac);
        }
    }
    // The picture where phase eats frequencies: per-mic curves + the thick blend curve, columns
    // tinted red where the blend sits below the incoherent power sum (cancellation) / green above.
    void renderBlendOverlay() {
        if (reviewTab.mixRows.empty() || lastIRs.empty()) return;
        const auto strips = gatherStrips(); const auto master = gatherMaster();
        std::vector<SpectrumView::Trace> tr;
        std::vector<std::vector<double>> micC(lastIRs.size());
        for (size_t m = 0; m < lastIRs.size(); ++m) {                  // per-mic curve WITH its filters + gain
            std::vector<float> gi = ocap::processedMic(lastIRs[m], strips[m], lastIRSr);
            const double g = std::pow(10.0, strips[m].gainDb / 20.0);
            for (auto& v : gi) v *= (float)g;
            micC[m] = SpectrumView::makeCurve(gi, lastIRSr, false);
        }
        const auto blend = computeBlend();                             // WITH global filter → white curve shows the rolloff
        auto blendC = SpectrumView::makeCurve(blend, lastIRSr, false);
        if (blendC.empty()) return;
        // The interference tint reads the PRE-filter blend, so the HPF/LPF rolloff isn't mistaken for
        // phase cancellation (which was painting the whole stopband red).
        auto rawC = SpectrumView::makeCurve(computeBlend(false), lastIRSr, false);
        // Master-filter magnitude (|H|, ~1 in the passband → 0 in the stopbands), used to GATE the
        // interference tint: no phase-cancellation red where the Master HPF/LPF has removed the signal.
        std::vector<double> mMag;
        if (ocap::hpActiveSlope(master.hpf) || ocap::lpActiveSlope(master.lpf)) {
            std::vector<float> imp(lastIRs[0].size(), 0.0f); imp[0] = 1.0f;
            ocap::applyFilters(imp, master.hpf, master.lpf, lastIRSr);
            auto mc = SpectrumView::makeCurve(imp, lastIRSr, false);   // dB, 0 at passband
            mMag.resize(mc.size());
            for (size_t p = 0; p < mc.size(); ++p) mMag[p] = std::pow(10.0, std::min(0.0, mc[p]) / 20.0);
        }
        // One shared display reference, ANCHORED when the take loads: the initial blend peak
        // + 3 dB headroom, then never re-derived — so gain moves visibly move the picture
        // (per-render re-normalizing made Master -24 dB look like nothing happened).
        if (std::isnan(overlayRefDb)) {
            double pk = -1e9;
            for (double v : blendC) pk = std::max(pk, v);
            overlayRefDb = pk + 6.0;
        }
        const double ref = overlayRefDb;
        // v0.7.0: interference (coherent − incoherent power sum, where phase eats/reinforces) comes from
        // core; the Master-stopband tint gate is a display weight (not valid dB math), so it stays here.
        namespace off = felitronics::analysis::offline;
        std::vector<double> coherent(blendC.size());
        for (size_t p = 0; p < blendC.size(); ++p) coherent[p] = (p < rawC.size()) ? rawC[p] : blendC[p];
        std::vector<off::MicCurveView> mv; mv.reserve(micC.size());
        for (size_t m = 0; m < micC.size(); ++m)
            mv.push_back({ std::span<const double>(micC[m]), ocap::channelAudible(strips, m) });
        std::vector<double> interf = off::interferenceDb(coherent, mv);
        for (size_t p = 0; p < interf.size() && p < mMag.size(); ++p) interf[p] *= mMag[p];   // fade tint in the Master's stopband
        int activeMic = -1;                                            // the selected channel (its curve pops)
        for (size_t m = 0; m < reviewTab.mixRows.size(); ++m) if (activeStrip == reviewTab.mixRows[m].get()) activeMic = (int)m;
        const bool masterActive = (activeStrip == nullptr) || (reviewTab.masterStrip && activeStrip == reviewTab.masterStrip.get());
        for (size_t m = 0; m < micC.size(); ++m) {                     // non-active mics first (dimmed underneath)
            if ((int)m == activeMic) continue;
            if (!ocap::channelAudible(strips, m)) continue;            // muted/off-solo mics leave the graph (legend keeps them)
            for (auto& v : micC[m]) v -= ref;
            const float a = activeMic >= 0 || masterActive ? 0.5f : 0.75f;
            tr.push_back({ std::move(micC[m]), lastIRColours[m].withAlpha(a), 1.0f, false });
        }
        for (auto& v : blendC) v -= ref;                               // the mix (white); brighter/thicker when Master is active
        tr.push_back({ std::move(blendC), juce::Colours::white.withAlpha(masterActive ? 1.0f : 0.7f), masterActive ? 2.6f : 1.7f, true });
        if (activeMic >= 0 && ocap::channelAudible(strips, (size_t)activeMic)) {   // the selected mic's curve, highlighted on top
            for (auto& v : micC[(size_t)activeMic]) v -= ref;
            tr.push_back({ std::move(micC[(size_t)activeMic]), lastIRColours[(size_t)activeMic], 2.6f, false });
        }
        reviewTab.spectrumView.setTraces(std::move(tr), std::move(interf));
    }
    // ---- Export: deliverable files (pro-lib style folders: rates x lengths, 24-bit PCM) ----
    // One deliverable: resampled -> truncated to `len` -> short fade-out -> 24-bit PCM.
    static bool writeDeliverable(const juce::File& f, const std::vector<double>& full, double sr, int len) {
        const int n = std::min<int>(len, (int)full.size());
        if (n < 8) return false;
        std::vector<double> out(full.begin(), full.begin() + n);
        const int fade = std::min(64, n / 8);                          // click-free truncation
        for (int i = 0; i < fade; ++i)
            out[(size_t)(n - fade + i)] *= 0.5 * (1.0 + std::cos(juce::MathConstants<double>::pi * (double)(i + 1) / fade));
        return oc::wav_write(f.getFullPathName().toStdString(), { out }, sr, 24, false);
    }
    // Deliverable naming lives in core/ExportPlanner.h now (headless-tested); this shim keeps the
    // lastIRFileBases construction sites unchanged.
    static juce::String sanitizeName(const juce::String& s) { return juce::String(ocap::exportplan::sanitizeName(s.toStdString())); }
    // Write the deliverables for the CURRENT take (per-mic IRs + the mixer MIX) into `dir` per the
    // headless-tested plan (toggles + naming → exact file list, core/ExportPlanner.h). Returns the
    // file count. Shared by "Export to folder" and the session bundle so the zip carries the same
    // MIX + length/rate variants.
    int writeDeliverablesTo(const juce::File& dir) {
        if (lastIRs.empty()) return 0;
        const auto sm = currentSessionMeta();
        ocap::exportplan::Naming naming { sm.author, sm.cabModel, {} };
        for (const auto& b : lastIRFileBases) naming.micFileBases.push_back(b.toStdString());
        const auto files = ocap::exportplan::plan(exportTab.selection(), naming, (int)lastIRs.size());

        std::vector<float> blend;                                      // the MIX source, built lazily
        auto sourceIR = [&](int src) -> const std::vector<float>& {
            if (src != ocap::exportplan::kMixSource) return lastIRs[(size_t)src];
            if (blend.empty()) {                                       // mixer settings + 0.98 peak-normalize
                blend = computeBlend();
                double pk = 0.0; for (float v : blend) pk = std::max(pk, (double)std::abs(v));
                if (pk > 0.0) for (auto& v : blend) v = (float)(v * 0.98 / pk);
            }
            return blend;
        };
        int written = 0;
        int cachedSrc = -2, cachedSr = 0; std::vector<double> resampled;   // plan is rate-major → one-slot cache
        for (const auto& f : files) {
            const auto out = dir.getChildFile(f.relPath);
            if (f.raw) {                                               // untouched mic'd sweep at capture rate
                if (currentTake < 0 || currentTake >= (int)takeDirs.size()) continue;
                const auto rf = takeDirs[(size_t)currentTake].getChildFile(f.srcFileName);
                out.getParentDirectory().createDirectory();
                if (rf.existsAsFile() && rf.copyFileTo(out)) ++written;
                continue;
            }
            const auto& src = sourceIR(f.source);
            if (src.empty()) continue;
            if (f.source != cachedSrc || (int)f.targetSr != cachedSr) {
                resampled = ocap::resampleIR(src, lastIRSr, f.targetSr);
                cachedSrc = f.source; cachedSr = (int)f.targetSr;
            }
            out.getParentDirectory().createDirectory();
            if (writeDeliverable(out, resampled, f.targetSr, f.lenSamples)) ++written;
        }
        return written;
    }
    void doExport(const juce::File& dir) {
        if (lastIRs.empty()) { exportTab.exportStatus.setText("Nothing captured yet.", juce::dontSendNotification); return; }
        const int written = writeDeliverablesTo(dir);
        exportTab.exportStatus.setText(written > 0
            ? juce::String(written) + " files exported to " + dir.getFullPathName()
            : "Nothing exported - check the boxes (and capture first).", juce::dontSendNotification);
    }

    void handleAsyncUpdate() override {
        const int N = engine.rtNumMics;
        oc::SweepSpec s; s.sr = engine.sampleRate;
        const oc::Sweep sw = oc::make_sweep(s);
        std::vector<std::vector<double>> recs((size_t)N);
        for (int m = 0; m < N; ++m) recs[(size_t)m].assign(engine.recordedM[(size_t)m].begin(), engine.recordedM[(size_t)m].end());
        const ocap::CaptureResult cap = ocap::runCapturePipeline(recs, sw, engine.sampleRate);
        const auto& gs = cap.gates;
        if (!cap.ok) {
            juce::String fails;
            for (int m = 0; m < N; ++m)
                if (!gs[(size_t)m].ok)
                    fails << "\n  mic" << juce::String(m + 1) << " (in" << juce::String(engine.rtChans[m] + 1) << "): "
                          << gs[(size_t)m].reason.c_str() << " (peak " << juce::String(gs[(size_t)m].peak_dbfs, 1)
                          << " dBFS, snr " << juce::String(gs[(size_t)m].snr, 0) << ")";
            takeTab.status.setText("REJECTED - whole set (retake is cheap):" + fails, juce::dontSendNotification);
            return;
        }
        const auto& outs = cap.irs;
        const std::size_t refLag = cap.refLag;
        lastIRs.clear(); lastIRNames.clear(); lastIRColours.clear(); lastIRFileBases.clear();
        lastIRSr = engine.sampleRate;
        for (int m = 0; m < N; ++m) {
            lastIRs.emplace_back(outs[(size_t)m].begin(), outs[(size_t)m].end());
            const auto& r = *takeTab.micRows[(size_t)juce::jmin(rtRowIdx[m], (int)takeTab.micRows.size() - 1)];
            const juce::String nm = "mic" + juce::String(m + 1) + " " +
                (r.mic.getText().isNotEmpty() ? r.mic.getText() : juce::String("?")) + " (in" + juce::String(engine.rtChans[m] + 1) + ")";
            lastIRNames.add(nm);
            lastIRColours.push_back(kSlotColours[r.slot]);
            const juce::String model = r.mic.getText().isNotEmpty() ? r.mic.getText() : "mic" + juce::String(m + 1);
            const juce::String pos = (r.location.getText() == "grille" ? r.position : r.location.getText());
            const juce::String dist = r.dist.getText().isNotEmpty() ? r.dist.getText() + distUnitText() : juce::String();
            const juce::String desc = (pos + " " + dist).trim();
            lastIRFileBases.add(sanitizeName(model + (desc.isNotEmpty() ? " - " + desc : juce::String())));
        }
        reviewTab.spectrumView.setIR(lastIRs[0], engine.sampleRate);
        rebuildMixer();
        refreshSpectrum();
        reviewTab.reviewInfo.setText("Ready - audition the last capture (" + juce::String(N) + " mic"
                         + (N > 1 ? "s" : "") + ") through a DI riff below.", juce::dontSendNotification);
        juce::String metrics;
        for (int m = 0; m < N; ++m)
            metrics << (m ? " | " : "") << "in" << juce::String(engine.rtChans[m] + 1)
                    << " peak " << juce::String(gs[(size_t)m].peak_dbfs, 1) << " dB, SNR " << juce::String(gs[(size_t)m].snr_db, 1)
                    << " dB, dly +" << juce::String((int)cap.delaySamples[(size_t)m]);
        double worstSnr = 1e9; for (int m = 0; m < N; ++m) worstSnr = std::min(worstSnr, gs[(size_t)m].snr_db);
        if (worstSnr < 45.0) metrics << "\nNOISY (SNR " + juce::String(worstSnr, 1) + " dB) - capture louder for a cleaner IR tail.";
        saveTakeToSession(recs, outs, sw, cap, gs, refLag, metrics);
    }

    // Every good capture is a TAKE saved straight into the session folder (crash-proof, no zips).
    // App id + timestamp + the cabinet snapshot (the same fields the cab form autosaves) —
    // shared by captured and imported takes.
    void stampTakeHeader(ocap::TakeMeta& take) const {
        take.app = "OrbitCapture 0.1";
        take.timestamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S").toStdString();
        take.sampleRate = engine.sampleRate;
        const auto sm = currentSessionMeta();
        take.instrument = sm.instrument; take.enclosure = sm.enclosure;
        take.cabModel = sm.cabModel; take.speaker = sm.speaker;
        take.speakerCount = juce::String(sm.speakerCount).getIntValue();
        take.speakerSizeIn = sm.speakerSizeIn;
        take.tweeter = sm.tweeter == "tweeter";
        take.back = sm.back;
        take.ampModel = sm.amp; take.ampType = sm.ampType;
        take.room = sm.room;
    }
    // Register a freshly-written take dir and land the user on it (shared capture/import tail).
    void adoptNewTake(const juce::File& dir) {
        takeDirs.push_back(dir);
        currentTake = (int)takeDirs.size() - 1;
        refreshTakeBox();
        saveSessionJson();
        refreshExportStatus();                                         // the new take may add mic-metadata warnings
        takeTab.navToReview.setEnabled(true);                          // there's something to mix now
    }
    void saveTakeToSession(const std::vector<std::vector<double>>& recs, const std::vector<std::vector<double>>& irs,
                           const oc::Sweep& sw, const ocap::CaptureResult& cap,
                           const std::vector<oc::GateReport>& gs, std::size_t refLag, const juce::String& metrics) {
        const int N = (int)irs.size();
        if (sessionDir == juce::File()) createSessionDir();            // capture before naming? still safe

        ocap::TakeMeta take;
        stampTakeHeader(take);
        take.inputChannel = engine.rtChans[0] + 1;
        take.interface_ = lastDevName.toStdString();                   // the capture chain's converter (additive field)
        // mics[] (additive) + mic = mics[0] (v1-schema compat: old readers keep working) — takeToVar mirrors this
        for (int m = 0; m < N; ++m) {
            const auto& r = *takeTab.micRows[(size_t)juce::jmin(rtRowIdx[m], (int)takeTab.micRows.size() - 1)];
            ocap::MicMeta mm;
            mm.model = r.mic.getText().toStdString();
            mm.location = r.location.getText().toStdString();
            mm.position = (r.location.getText() == "grille" ? r.position : r.location.getText()).toStdString();
            mm.axis = r.axis.getText().toStdString();
            mm.distanceMm = juce::roundToInt(rowDistanceMm(r));
            mm.distanceInput = (r.dist.getText() + " " + distUnitText()).toStdString();
            mm.inputChannel = engine.rtChans[m] + 1;
            mm.gatePeakDbfs = gs[(size_t)m].peak_dbfs;
            mm.gateSnr = gs[(size_t)m].snr;
            mm.snrDb = gs[(size_t)m].snr_db;                           // honest band-limited measurement SNR
            mm.latencySamples = (int)cap.latency[(size_t)m];
            mm.delaySamples = (std::size_t)cap.delaySamples[(size_t)m]; // vs the earliest mic
            mm.slot = r.slot;                                          // mic colour identity
            take.mics.push_back(mm);
        }
        // measured / derived (primary mic, v1 compat)
        take.measured = { gs[0].peak_dbfs, gs[0].snr, gs[0].snr_db, gs[0].clip_run, (int)cap.latency[0], irs[0].size() };
        // sweep spec (reproducibility)
        take.sweep = { sw.spec.f1, sw.spec.f2, sw.spec.dur, sw.spec.tail };
        // default mix: unity, no invert (mix/master land at the full StripParams/MasterParams schema —
        // takeToVar's already-shipped, already-tested fidelity — richer than the old capture-time 2-key
        // stub, but strictly additive/harmless: old app builds ignore unknown keys, and readStrip/
        // stripFromVar already default any of these fields when absent). take.master stays default
        // (unity, filters off) — the actual Master state only exists once rebuildMixer() below runs.
        for (int m = 0; m < N; ++m) take.mix.push_back(ocap::StripParams{});

        const auto dir = store_.saveTake(sessionDir, take, recs, irs, engine.sampleRate);
        adoptNewTake(dir);
        takeTab.status.setText(dir.getFileName() + " saved to " + sessionDir.getFileName()
                     + "  (" + juce::String(N) + " mic" + (N > 1 ? "s" : "") + ", one sweep)\n" + metrics,
                       juce::dontSendNotification);
    }

    // A named EMPTY take: the user gives it a name, then fills it with IR files via the
    // console's [+]. Killed the old files->new-take flow: its synthetic mic-join label was long
    // and stale after channel edits — a user-given name stays honest.
    void newEmptyTake() {
        textPrompt("New take name", "", [this](juce::String nm) {
            if (sessionDir == juce::File()) createSessionDir();
            ocap::TakeMeta take;
            stampTakeHeader(take);
            take.name = nm.toStdString();
            take.sampleRate = 0.0;                                     // the first added channel sets it
            take.interface_ = "file import";
            const auto dir = store_.saveTake(sessionDir, take, {}, {}, engine.sampleRate);
            adoptNewTake(dir);
            loadTake(currentTake);
            reviewTab.reviewInfo.setText("Empty take \"" + nm + "\" - add IR files with the [+] next to the console.",
                                         juce::dontSendNotification);
        });
    }

    // ---- session persistence ----
    static juce::File sessionsRoot() {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("OrbitCapture").getChildFile("sessions");
    }
    void createSessionDir(juce::String nm = {}) {
        nm = nm.trim();
        if (nm.isEmpty()) nm = exportTab.cab.cabModel.getText().trim();   // fall back to the cab model, then "session"
        sessionDir = store_.createSession(nm);                         // store_ owns the "session" fallback + stamp/sanitize
        takeDirs.clear(); currentTake = -1;
        refreshTakeBox();
        saveSessionJson();
    }
    ocap::SessionMeta currentSessionMeta() const {                     // cab form + author + units
        ocap::SessionMeta s;
        exportTab.cab.toMeta(s);
        s.author = exportTab.authorField.getText().toStdString();
        s.distUnit = distUnitText().toStdString();
        return s;
    }
    void saveSessionJson() {
        if (sessionDir == juce::File()) return;
        store_.saveSessionJson(sessionDir, currentSessionMeta());
    }
    void loadSessionJson() {
        const auto s = store_.loadSessionJson(sessionDir);
        exportTab.cab.fromMeta(s);
        { const juce::String u = s.distUnit;
          if (u == "in" || u == "cm")
              for (int i = 0; i < distUnit.getNumItems(); ++i)
                  if (distUnit.getItemText(i) == u) { distUnit.setSelectedId(distUnit.getItemId(i), juce::dontSendNotification); break; } }
        { const juce::String a = s.author; exportTab.authorField.setText(a.isNotEmpty() ? a : juce::String("Darwin's Cat"), juce::dontSendNotification); }
        prevInches = distIsInches();
    }
    void loadLatestSessionOrNew() {
        sessionsRoot().createDirectory();
        juce::File latest; juce::Time lt;
        for (const auto& d : sessionsRoot().findChildFiles(juce::File::findDirectories, false))
            if (d.getLastModificationTime() > lt) { lt = d.getLastModificationTime(); latest = d; }
        if (latest != juce::File()) {
            sessionDir = latest;
            loadSessionJson();
            scanTakes();
            if (!takeDirs.empty()) loadTake((int)takeDirs.size() - 1);
        } else {
            createSessionDir();                  // first run: an auto-named blank session; cabinet filled in Export
        }
        validateCabForm();
        updateSessionLabel();
    }
    void showSetupDialog() {
        juce::DialogWindow::LaunchOptions opt;
        opt.content.setNonOwned(&setupPanel);
        opt.dialogTitle = "Setup";
        opt.dialogBackgroundColour = juce::Colour(0xff23252b);
        opt.escapeKeyTriggersCloseButton = true;
        opt.useNativeTitleBar = false;
        opt.resizable = false;
        auto* dw = opt.launchAsync();
        dw->centreAroundComponent(this, setupPanel.getWidth() + 20, setupPanel.getHeight() + 40);
    }
    void updateSessionLabel() { refreshSessionBox(); }
    void refreshSessionBox() {                                         // all sessions, newest first
        sessionList.clear();
        auto ds = sessionsRoot().findChildFiles(juce::File::findDirectories, false);
        std::sort(ds.begin(), ds.end(), [](const juce::File& a, const juce::File& b) {
            return a.getLastModificationTime() > b.getLastModificationTime(); });
        sessionBox.clear(juce::dontSendNotification);
        int sel = 0;
        for (int i = 0; i < ds.size(); ++i) {
            sessionList.push_back(ds[i]);
            sessionBox.addItem(ds[i].getFileName(), i + 1);
            if (ds[i] == sessionDir) sel = i + 1;
        }
        sessionBox.setSelectedId(sel, juce::dontSendNotification);
    }
    void switchToSession(const juce::File& d) {
        saveSessionJson();                                             // persist the one we're leaving
        sessionDir = d;
        loadSessionJson();
        scanTakes();
        if (!takeDirs.empty()) loadTake((int)takeDirs.size() - 1);
        else clearReviewState();
        refreshSessionBox();
        validateCabForm();                                             // refresh cab-form red outlines for the new session
    }
    void restoreSession(const juce::File& d) {
        sessionDir = d;
        if (d != juce::File() && d.exists()) {
            loadSessionJson(); scanTakes();
            if (!takeDirs.empty()) loadTake((int)takeDirs.size() - 1); else clearReviewState();
        } else { takeDirs.clear(); currentTake = -1; clearReviewState(); refreshTakeBox(); }
        validateCabForm();                                             // live cab-form red + export warnings
    }
    // ---- the console IS the take editor: per-channel edit / delete / append ----
    // A small callout anchored to the strip — the channel's whole mutable-forever metadata:
    // model (a new name lands in the user mic list), axis, position, optional distance.
    void showChannelEditor(int m) {
        if (currentTake < 0 || currentTake >= (int)takeDirs.size()
            || m < 0 || m >= (int)reviewTab.mixRows.size()) return;
        const auto dir = takeDirs[(size_t)currentTake];
        const auto meta = store_.loadTakeMeta(dir);
        if (m >= (int)meta.mics.size()) return;
        struct Panel : juce::Component {
            juce::ComboBox model, axis, position;
            juce::TextEditor dist; juce::Label distLbl { {}, "distance" }, unitLbl;
            juce::TextButton save { "Save" };
            void resized() override {
                auto r = getLocalBounds().reduced(10);
                model.setBounds(r.removeFromTop(26)); r.removeFromTop(6);
                axis.setBounds(r.removeFromTop(26)); r.removeFromTop(6);
                position.setBounds(r.removeFromTop(26)); r.removeFromTop(6);
                { auto a = r.removeFromTop(26);
                  distLbl.setBounds(a.removeFromLeft(66));
                  dist.setBounds(a.removeFromLeft(64)); a.removeFromLeft(6);
                  unitLbl.setBounds(a.removeFromLeft(30)); }
                r.removeFromTop(8);
                save.setBounds(r.removeFromTop(26).removeFromRight(80));
            }
        };
        auto panel = std::make_unique<Panel>();
        panel->setSize(240, 200);
        for (juce::Component* c : { (juce::Component*)&panel->model, (juce::Component*)&panel->axis,
                                    (juce::Component*)&panel->position, (juce::Component*)&panel->dist,
                                    (juce::Component*)&panel->distLbl, (juce::Component*)&panel->unitLbl,
                                    (juce::Component*)&panel->save })
            panel->addAndMakeVisible(c);
        panel->model.setEditableText(true);                            // type a NEW model -> user mic list
        panel->model.setTextWhenNothingSelected("mic model");
        refreshMicCombo(panel->model, juce::String(meta.mics[(size_t)m].model));
        for (int a = 0; a < vocab::axis.size(); ++a) panel->axis.addItem(vocab::axis[a], a + 1);
        panel->axis.setTextWhenNothingSelected("axis");
        { const juce::String ax(meta.mics[(size_t)m].axis);
          if (ax.isNotEmpty()) panel->axis.setSelectedId(vocab::axis.indexOf(ax) + 1, juce::dontSendNotification); }
        panel->position.setEditableText(true);
        panel->position.setTextWhenNothingSelected("position (optional)");
        for (int i = 0; i < vocab::positions.size(); ++i) panel->position.addItem(vocab::positions[i], i + 1);
        { const juce::String pos(meta.mics[(size_t)m].position);
          if (pos.isNotEmpty()) panel->position.setText(pos, juce::dontSendNotification); }
        panel->dist.setInputRestrictions(6, "0123456789.");
        panel->dist.setTextToShowWhenEmpty("-", juce::Colours::grey);
        if (meta.mics[(size_t)m].distanceMm > 0) {
            const double v = distIsInches() ? meta.mics[(size_t)m].distanceMm / 25.4
                                            : meta.mics[(size_t)m].distanceMm / 10.0;
            panel->dist.setText(juce::String(v, (v == (double)(int)v) ? 0 : 1), false);
        }
        panel->unitLbl.setText(distUnitText(), juce::dontSendNotification);
        auto* p = panel.get();
        p->save.onClick = [this, m, p, dir] {
            const juce::String mdl = p->model.getText().trim();
            if (mdl.isNotEmpty() && !listStore.get("mic").contains(mdl, true)) {
                listStore.add("mic", mdl);                             // a typed model joins the user mic list
                for (auto& mr : takeTab.micRows) refreshMicCombo(mr->mic, mr->mic.getText());
            }
            const auto v = juce::JSON::parse(dir.getChildFile("take.json").loadFileAsString());
            if (auto* arr = v.getProperty("mics", juce::var()).getArray())
                if (m < arr->size())
                    if (auto* mo = (*arr)[m].getDynamicObject()) {
                        mo->setProperty("model", mdl);
                        mo->setProperty("axis",  p->axis.getText());
                        mo->setProperty("position", p->position.getText().trim());
                        const double dv = p->dist.getText().getDoubleValue();
                        if (p->dist.getText().trim().isNotEmpty() && dv > 0.0) {
                            mo->setProperty("distance_mm", juce::roundToInt(distIsInches() ? dv * 25.4 : dv * 10.0));
                            mo->setProperty("distance_input", p->dist.getText().trim() + " " + distUnitText());
                        } else {
                            mo->setProperty("distance_mm", 0);
                            mo->setProperty("distance_input", juce::String());
                        }
                        if (auto* o = v.getDynamicObject()) o->setProperty("mic", (*arr)[0]);   // v1 mirror
                        dir.getChildFile("take.json").replaceWithText(juce::JSON::toString(v));
                    }
            if (auto* box = p->findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
            loadTake(currentTake);                                     // refresh names / legend / labels
            refreshTakeBox();
            refreshExportStatus();
        };
        const auto anchor = getLocalArea(reviewTab.mixRows[(size_t)m].get(),
                                         reviewTab.mixRows[(size_t)m]->getLocalBounds());
        juce::CallOutBox::launchAsynchronously(std::move(panel), localAreaToGlobal(anchor), nullptr);
    }
    void confirmDeleteChannel(int m) {
        if (currentTake < 0 || currentTake >= (int)takeDirs.size()
            || m < 0 || m >= (int)reviewTab.mixRows.size() || reviewTab.mixRows.size() < 2) return;
        const auto dir = takeDirs[(size_t)currentTake];
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Delete channel",
            "Delete " + reviewTab.mixRows[(size_t)m]->name.getText() + " from " + dir.getFileName()
            + "? Its audio files are removed from the take - this cannot be undone.",
            "Delete", "Cancel", this,
            juce::ModalCallbackFunction::create([this, m, dir](int okPressed) {
                if (okPressed != 1) return;
                if (!store_.removeChannel(dir, m)) return;
                loadTake(currentTake);
                refreshTakeBox();
                refreshExportStatus();
            }));
    }
    // [+] on the console: append IR file(s) to THIS take — resampled to the take's rate, trimmed/
    // padded to its length, onset-aligned to channel 1 so the blend shares one time reference.
    void appendIrFiles() {
        if (currentTake < 0 || currentTake >= (int)takeDirs.size()) return;
        importChooser = std::make_unique<juce::FileChooser>("Add IR files to this take",
                                                            juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        importChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                       | juce::FileBrowserComponent::canSelectMultipleItems,
            [this](const juce::FileChooser& fc) {
                const auto files = fc.getResults();
                if (files.isEmpty()) return;
                const auto dir = takeDirs[(size_t)currentTake];
                bool haveRef = !lastIRs.empty();                       // a fresh named take: the first file rules
                std::ptrdiff_t refOnset = haveRef ? ocap::irimport::onsetIndex(lastIRs[0]) : 0;
                size_t len = haveRef ? lastIRs[0].size() : 0;
                double targetSr = haveRef ? lastIRSr : 0.0;
                auto meta = store_.loadTakeMeta(dir);
                juce::StringArray skipped;
                int added = 0;
                for (const auto& f : files) {
                    if ((int)meta.mics.size() >= kMaxMics) { skipped.add(f.getFileName() + " (take is full)"); continue; }
                    std::unique_ptr<juce::AudioFormatReader> rd(diFormats.createReaderFor(f));
                    if (rd == nullptr || rd->lengthInSamples <= 0 || rd->sampleRate <= 0) { skipped.add(f.getFileName()); continue; }
                    juce::AudioBuffer<float> b((int)rd->numChannels, (int)rd->lengthInSamples);
                    rd->read(&b, 0, (int)rd->lengthInSamples, 0, true, true);
                    std::vector<float> mono(b.getReadPointer(0), b.getReadPointer(0) + b.getNumSamples());
                    if (targetSr <= 0.0) targetSr = rd->sampleRate;    // empty take: adopt the first file's rate
                    std::vector<double> ir = std::abs(rd->sampleRate - targetSr) < 1.0
                        ? std::vector<double>(mono.begin(), mono.end())
                        : ocap::resampleIR(mono, rd->sampleRate, targetSr);
                    if (len == 0) len = std::min(ir.size(), (size_t)std::llround(targetSr * 2.0));   // first channel sets the length (2 s cap)
                    ir.resize(len, 0.0);                               // the set's common length
                    if (!haveRef) { refOnset = ocap::irimport::onsetIndex(ir); haveRef = true; }   // 1st channel IS the reference
                    else ocap::irimport::alignOnsetTo(ir, refOnset);   // others join its time base
                    ocap::MicMeta mm;
                    mm.model = f.getFileNameWithoutExtension().toStdString();
                    mm.location = "import";
                    mm.inputChannel = (int)meta.mics.size() + 1;
                    { std::vector<ocap::micset::MicRowSpec> specs;     // first free colour slot
                      for (const auto& x : meta.mics) specs.push_back({ x.location, x.position, 0.0, x.inputChannel - 1, x.slot });
                      mm.slot = juce::jmax(0, ocap::micset::firstFreeSlot(specs, kMaxMics)); }
                    if (store_.appendChannel(dir, mm, ir, targetSr)) { ++added; meta.mics.push_back(mm); }
                    else skipped.add(f.getFileName());
                }
                loadTake(currentTake);
                refreshTakeBox();
                refreshExportStatus();
                juce::String info = dir.getFileName() + ": added " + juce::String(added) + " channel"
                                  + (added == 1 ? "" : "s") + " (onset-aligned to the set).";
                if (!skipped.isEmpty()) info << "  Skipped: " << skipped.joinIntoString(", ") << ".";
                reviewTab.reviewInfo.setText(info, juce::dontSendNotification);
            });
    }
    void scanTakes() {
        takeDirs = store_.scanTakes(sessionDir);
        currentTake = takeDirs.empty() ? -1 : (int)takeDirs.size() - 1;
        refreshTakeBox();
    }
    juce::String takeLabelFor(const juce::File& d) const {
        const auto t = store_.loadTakeMeta(d);                         // legacy-migrated → v0 takes label too
        if (!t.name.empty())                                           // a user-given name stays honest through edits
            return d.getFileName() + "  " + ocap::report::takeTime(t.timestamp) + "   " + juce::String(t.name)
                 + "  (" + juce::String((int)t.mics.size()) + " ch)";
        juce::String mics;
        for (const auto& m : t.mics)
            mics << (mics.isEmpty() ? "" : " + ") << (m.model.empty() ? "?" : juce::String(m.model))
                 << " " << juce::String(m.position);
        return d.getFileName() + "  " + ocap::report::takeTime(t.timestamp) + "   " + mics;
    }
    void refreshTakeBox() {
        reviewTab.takeBox.clear(juce::dontSendNotification);
        for (int i = 0; i < (int)takeDirs.size(); ++i) reviewTab.takeBox.addItem(takeLabelFor(takeDirs[(size_t)i]), i + 1);
        if (currentTake >= 0 && currentTake < (int)takeDirs.size())
            reviewTab.takeBox.setSelectedId(currentTake + 1, juce::dontSendNotification);
        updateSessionLabel();
    }
    void clearReviewState() {
        lastIRs.clear(); lastIRNames.clear(); lastIRColours.clear(); lastIRFileBases.clear();
        rebuildMixer();
        reviewTab.spectrumView.setIR({}, 0.0);
    }
    // Load a saved take back into Review: IRs from disk, names/colours/mix from take.json. store_.loadTake
    // already applies the pre-'mics' take (v0/v1) -> [mic] fallback (SessionVar.h's takeFromVar).
    void loadTake(int idx) {
        if (idx < 0 || idx >= (int)takeDirs.size()) return;
        currentTake = idx;
        const auto dir = takeDirs[(size_t)idx];
        const auto lt = store_.loadTake(dir);
        lastIRs.clear(); lastIRNames.clear(); lastIRColours.clear(); lastIRFileBases.clear();
        const int N = (int)lt.irs.size();
        if (N > 0) lastIRSr = lt.sampleRate;
        for (int m = 0; m < N; ++m) {
            lastIRs.emplace_back(lt.irs[(size_t)m].begin(), lt.irs[(size_t)m].end());
            const auto& mm = lt.take.mics[(size_t)m];
            const juce::String model = mm.model;
            const juce::String nm = "mic" + juce::String(m + 1) + " " + (model.isEmpty() ? "?" : model)
                                  + " (in" + juce::String(mm.inputChannel) + ")";
            lastIRNames.add(nm);
            lastIRColours.push_back(kSlotColours[juce::jlimit(0, kMaxMics - 1, mm.slot)]);
            juce::String pos = mm.position;
            if (pos.isEmpty()) pos = mm.location;
            const juce::String dist = juce::String(mm.distanceInput).removeCharacters(" ");
            const juce::String desc = (pos + " " + dist).trim();
            const juce::String mdl = model.isEmpty() ? "mic" + juce::String(m + 1) : model;
            lastIRFileBases.add(sanitizeName(mdl + (desc.isNotEmpty() ? " - " + desc : juce::String())));
        }
        rebuildMixer();
        suppressMixSave = true;                                        // restore the saved mix without re-saving it
        for (int m = 0; m < (int)reviewTab.mixRows.size() && m < (int)lt.take.mix.size(); ++m)
            reviewTab.mixRows[(size_t)m]->setParams(lt.take.mix[(size_t)m]);
        if (reviewTab.masterStrip) reviewTab.masterStrip->setMasterParams(lt.take.master);
        if (lt.take.monoFilter && reviewTab.mixRows.size() == 1) {      // legacy mono takes: mono_filter -> strip 0
            auto p = reviewTab.mixRows[0]->params();
            p.hpf = lt.take.monoFilter->hpf; p.lpf = lt.take.monoFilter->lpf;
            reviewTab.mixRows[0]->setParams(p);
        }
        suppressMixSave = false;
        setActiveStrip(reviewTab.masterStrip ? reviewTab.masterStrip.get() : nullptr);     // Master is the default active strip; also refreshes the curve
        if (engine.conv.mode.load() != 0) reloadLiveIR();
        reviewTab.reviewInfo.setText({}, juce::dontSendNotification);            // the take is shown in the combo — no redundant line
    }
    void saveMixToTake() {
        if (suppressMixSave || currentTake < 0 || currentTake >= (int)takeDirs.size()) return;
        std::vector<ocap::StripParams> mix;
        mix.reserve(reviewTab.mixRows.size());
        for (const auto& mr : reviewTab.mixRows) mix.push_back(mr->params());
        // reviewTab.masterStrip only exists for >=2 mics (rebuildMixer) — nullopt here leaves an existing "master"
        // key (if any) untouched rather than stamping a bogus one onto a take that never had one.
        const std::optional<ocap::MasterParams> master =
            reviewTab.masterStrip ? std::optional<ocap::MasterParams>(reviewTab.masterStrip->masterParams()) : std::nullopt;
        const std::optional<ocap::StripParams> mono =                  // 1-channel: mirror the strip into the
            reviewTab.mixRows.size() == 1 ? std::optional<ocap::StripParams>(reviewTab.mixRows[0]->params())
                                          : std::nullopt;              // legacy mono_filter key (old readers)
        store_.saveMix(takeDirs[(size_t)currentTake], mix, master, mono);
        if (engine.conv.mode.load() != 0) reloadLiveIR();                             // keep the live monitor in sync with the mix
    }
    // The session bundle: everything (takes + report.md) in one zip on the Desktop. The gates and
    // status strings live here; the zip mechanics are persist/BundleExporter.h.
    void exportBundle() {
        const auto warn = exportWarnings();
        if (!warn.isEmpty()) {
            exportTab.exportStatus.setText("Cannot export the bundle - fill: " + warn.joinIntoString(", "), juce::dontSendNotification);
            return;
        }
        if (sessionDir == juce::File() || takeDirs.empty()) {
            exportTab.exportStatus.setText("Nothing to bundle - capture some takes first.", juce::dontSendNotification);
            return;
        }
        writeReportMd();
        writeReportHtml();                                             // report.html — opens in a browser by click
        const auto zipF = juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                              .getChildFile(sessionDir.getFileName() + "_bundle.zip");
        const auto res = ocap::exportSessionBundle(sessionDir, zipF,
            [this](const juce::File& d) { return writeDeliverablesTo(d); });
        exportTab.exportStatus.setText(res.ok
            ? "Session bundle saved (" + juce::String(res.deliverables) + " deliverables + raw takes): " + zipF.getFullPathName()
            : "Bundle zip FAILED", juce::dontSendNotification);
    }
    // report.md / report.html content comes from persist/ReportWriter.h (pure string builders,
    // headless-tested); this just gathers the data and writes the files into the session dir.
    std::vector<ocap::report::TakeRow> reportRows() const {
        std::vector<ocap::report::TakeRow> rows; rows.reserve(takeDirs.size());
        for (const auto& d : takeDirs) rows.push_back({ d.getFileName(), store_.loadTakeMeta(d) });
        return rows;
    }
    void writeReportMd() {
        sessionDir.getChildFile("report.md").replaceWithText(
            ocap::report::reportMd(sessionDir.getFileName(), currentSessionMeta(), lastDevName, reportRows()));
    }
    void writeReportHtml() {
        sessionDir.getChildFile("report.html").replaceWithText(
            ocap::report::reportHtml(sessionDir.getFileName(), currentSessionMeta(), lastDevName, reportRows()));
    }

    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
    FileListStore listStore;

    // session: the persistent workspace (session = cabinet = catalog entry)
    ocap::SessionStore store_ { sessionsRoot() };   // de-monolith step 6d: the take.json/session.json writer
    juce::File sessionDir;
    juce::ComboBox sessionBox;                 // session picker (header, top-right)
    std::vector<juce::File> sessionList;
    juce::TextButton newSessionBtn, setupBtn;     // + = new session · gear = Setup dialog
    Page setupPanel;                              // Setup dialog content (units + advanced review toggles)
    juce::Label unitLbl;                          // "Distance units" row label in Setup
    juce::TextButton setupCloseBtn { "Close" };
    std::vector<juce::File> takeDirs;
    int currentTake = -1;
    bool suppressMixSave = false;              // guard while loading a take's mix into the strips

    // mic rows (colour = mic; up to 4 grille + 2 room + 2 rear) — widgets live on takeTab
    static constexpr int kMaxMics = 8;
    int activeRow = 0;
    juce::ComboBox distUnit;                    // session-wide cm/in (header)
    bool prevInches = false;                    // last unit, for on-switch distance conversion
    juce::StringArray inNames;                 // current device input-channel names

    // capture engine: which rows record on which channels. recordedM/rtChans/rtNumMics/rtTotal now
    // live on `engine` (the RT audio callback, de-monolith step 7); rtRowIdx stays here — it's a
    // message-thread-only mic-row index, never touched by the RT callback.
    int rtRowIdx[kMaxMics] = {};
    ocap::AudioEngine engine;

    // review: per-mic IRs of the last run + the blend mixer
    std::vector<std::vector<float>> lastIRs;
    juce::StringArray lastIRNames;
    std::vector<juce::Colour> lastIRColours;
    KnobLNF knobLnf;                                 // phase/shift dials (value inside) — declared before reviewTab (outlives its strips)
    MixStrip* activeStrip = nullptr;                  // whose HPF/LPF the graph drag edits (default: Master)
    double overlayRefDb = std::numeric_limits<double>::quiet_NaN();   // per-take display anchor (initial blend peak + 6 dB)
    bool analyzerOn = true;                           // live-analyser overlay (the graph's gear menu)

    // ---- the four tab views (ui/tabs/): dumb widgets + layout; all wiring stays here ----
    AudioTab audioTab;
    CaptureTab takeTab;
    ReviewTab reviewTab;
    ExportTab exportTab { listStore };               // owns the CabForm (declared after listStore)
    // HPF/LPF sweep ranges (a strip's on/off is an explicit toggle now, not a parked-extreme sentinel).
    static constexpr double kShiftMs = 2.0;          // +/- time-shift range (ms); 0 = off

    std::unique_ptr<juce::FileChooser> exportChooser;
    std::unique_ptr<juce::FileChooser> importChooser;  // Mixer tab: import IR files as a take
    double lastIRSr = 48000.0;                 // capture-time rate of the master IRs
    juce::StringArray lastIRFileBases;         // per-mic name part: "AKG C414 - Cap 1in" (author+cab prepended at export)
    BrandHeader header;
    juce::TooltipWindow tooltips { this };     // one shared tooltip window — without it setTooltip is silent
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    // liveConv/liveScratch/liveChannel/liveMaxBlock + the conv stream now live on `engine`.
    // live spectrum analyser: audio thread pushes the playing output into a ring (engine.specRing/
    // specW/specPush); the timer FFTs it.
    std::vector<DiClip> factoryClips, userClips;      // built-in riffs · user samples (persist in app-data; DiClip above)
    std::unique_ptr<juce::FileChooser> diChooser;
    juce::AudioFormatManager diFormats;
    // the audition stream (buffer + playing/loop/pos/len) now lives on `engine.audition`.

    std::atomic<int> captureChannel { 0 };
    int lastNumInNames = -1; juce::String lastDevName;
};
