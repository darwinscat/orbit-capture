# OrbitCapture

**Capture guitar-cabinet impulse responses. Blend the mics. Ship a pro IR library — in one app.**

OrbitCapture is a free, open-source (AGPL-3.0) desktop app for capturing your own speaker-cabinet
IRs with up to **8 microphones at once**, blending them like a mixing console, and exporting a
loader-ready IR pack — folders, rates, lengths, file names and a session report included.

## Why it's different

**One sweep, the whole mic set.** Place up to 8 mics (4 on the grille, 2 room, 2 rear), play a
single ESS sweep, and get one time-coherent IR per mic. The app deconvolves offline, gates each
mic (peak / SNR / clipping — a bad mic rejects the whole set, because a coherent set is the
unit), and **aligns everything to a common time reference so the natural inter-mic delays
survive**. That relative timing *is* the sound of a multi-mic rig — most tools throw it away by
trimming each IR to its own onset; OrbitCapture keeps it, so your blends phase-interact exactly
like the real mics did in the room.

**A real blend mixer, not a gain knob.** Per-mic strips with gain, **phase rotation
(-180°…+180° broadband all-pass — not a delay)**, ±2 ms time shift, solo/mute, and per-strip
Butterworth HPF/LPF you shape by dragging the lines right on the response graph. A Master bus
sums it all with its own gain + filters.

**See the phase, don't guess it.** The spectrum view overlays every mic's curve with the blend
curve and tints the columns where the mics *cancel* (red) or *reinforce* (green) versus the
incoherent power sum — comb filtering is visible before you print the mix. A live FFT analyser
runs on top while you play.

**Hear it while you turn the knobs.** Audition any take through built-in DI riffs or your own
DI track, streamed live through the current mixer settings — or monitor your guitar through the
mix in real time (partitioned convolution, RT-safe). Dry/wet bypass A/B at the click of a button.

**Metadata that survives you.** Cabinet, speaker, mic model / position / distance / axis, amp,
room — controlled vocabularies with no silent defaults, editable forever (fix a forgotten mic
model weeks later; the captured audio never changes). The Export gate warns about anything
missing before a bundle leaves the app.

**Mix IRs you already have.** Import existing IR wav files (up to 8, any mix of sample rates)
as a take and use the full mixer + export chain on them — OrbitCapture works as a standalone
IR blender, not just a capture rig.

**Pro deliverables, automatically.** One click exports `44.1/48/96 kHz × 1024/2048/4096
samples / 200/500 ms` folders of 24-bit mono WAVs named like a commercial pack
(`Author - Cab - Mic - Position Distance.wav`, plus a `MIX (C414+SM57+R121)` render of your
blend), raw sweeps if you want them, and a session bundle zip with an HTML/Markdown report of
every take.

## Workflow

1. **Audio** — pick your interface, outputs and inputs.
2. **Capture** — place mics on the interactive grille grid (drag the dots), set levels against
   the built-in noise with per-mic history meters and a green-zone verdict, hit Capture. Every
   good take lands in the session folder immediately — crash-proof, no "save" step.
3. **Mixer** — pick a take, blend the mics, watch the interference tint, audition through DI
   riffs or play live through the mix. Mixer settings save into the take as you go. No capture
   rig handy? **Import up to 8 already-captured IR wav files as a take** (mixed sample rates
   are fine — they're resampled to a common rate) and blend those instead.
4. **Export** — fill the cabinet/amp form (the quality gate), choose rates × lengths × sources,
   export a folder or a whole session bundle zip.

Sessions are plain files (per-take folders with raw sweeps + IRs + JSON metadata) you can back
up, diff and script against — on macOS under `~/Library/Application Support/OrbitCapture/sessions/`.

## Download & build

Release binaries (macOS universal, Windows x64/arm64, Linux x86_64/arm64) are on the
[Releases](../../releases) page.

> macOS release builds are code-signed and notarized. If Gatekeeper still complains about an
> early build, first launch is right-click → Open.

Building from source needs CMake 3.22+, a C++20 compiler, and the
[felitronics-core](https://github.com/darwinscat/felitronics-core) DSP library checked out as a
sibling directory (JUCE 8 is fetched automatically):

```bash
git clone https://github.com/darwinscat/felitronics-core.git
git clone https://github.com/darwinscat/orbit-capture.git
cd orbit-capture
cmake -B app/build app && cmake --build app/build            # → OrbitCapture.app / binary
# headless test gates
cmake -B app/tests/build app/tests && cmake --build app/tests/build
ctest --test-dir app/tests/build
```

## Architecture (for contributors)

The DSP and all decision logic are headless and test-covered; JUCE stays at the edges:

| Layer | Contents | Tested |
|---|---|---|
| `app/src/core/` | capture pipeline, blend engine, export planner — std only, **no JUCE** | ctest, ~100% |
| `app/src/model/` | session/take/mix/mic-set data models + JSON round-trip | round-trip + legacy fixtures |
| `app/src/persist/` | session store, report writer, bundle zip | fixture + parity tests |
| `app/src/audio/` | the *only* real-time code (device callback + RT-safe buffer streams) | integration + stream-type tests |
| `app/src/ui/` | dumb views: widgets + layout, one header per class | — |

The heavy math (ESS deconvolution, mic-set alignment, Butterworth/phase/blend kernels, spectrum
curves) lives in `felitronics::` and is property-tested + NULL-tested against reference
implementations there.

## License

OrbitCapture is licensed under the **GNU Affero General Public License v3.0** — see
[`LICENSE`](LICENSE). It links [JUCE](https://juce.com) (used here under its GPL/AGPL
open-source terms). Any third-party components retain their own licenses.
