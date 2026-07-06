# GroovyDaisy — Design Spec (v2)

> Supersedes the design sections of PLAN.md. This is the *what and why*;
> implementation phases get planned separately once this is agreed.

## Vision

A **campfire jam box**. Several friends play together through one small,
speaker-connected device:

- A guitarist plugs in via audio input — heard live with FX, and can layer
  loops like a loop pedal.
- A keyboardist plays the synth via the KeyLab keys.
- A drummer plays the sample drums via the KeyLab pads.
- Everything can be looped, layered, tweaked, and undone, live, without
  stopping the music.

It is an *instrument for a group*, not a production tool for one person.
In one line: **a fully featured groovebox with the jamability of a
looper** — capture-first at the engine, DAW-legible at the screen.

### Design principles

1. **The box is the brain.** All sequencing, looping, and sound generation run
   on the Daisy Pod. It must remain fully playable with nothing attached but
   the KeyLab, a guitar, and a speaker.
2. **Visual feedback is external and replaceable.** Pod LEDs are not enough.
   The companion app (today: React + WebSerial on a laptop) is the current
   "screen"; later it becomes a phone/tablet app or a hardware display. The
   protocol and app core must be portable; the firmware must never *require*
   a connected UI to perform.
3. **Forgiving over precise.** Jams are sloppy. Undo, per-track clear, and
   optional quantize matter more than editing precision. Nothing should
   require stopping playback.
4. **Uniform loop model.** MIDI loops and audio loops are the same concept —
   a loop track with its own length, overdub, undo — differing only in
   payload. One mental model for players, one code path where possible.

### Confirmed decisions

| Decision | Choice |
|---|---|
| Guitar role | Full audio looper (monitor + FX + loop recording) |
| Players | One KeyLab shared (keys = synth, pads = drums) + guitar audio in |
| Loop lengths | Per-track (1/2/4/8 bars) against a shared master clock |
| Record model | Retrospective capture — grab the last N bars from always-on rolling buffers; no record-arm |
| Track model | Every capture is a new track — no layer hierarchy; the mixer *is* the layer control |
| Undo | Undo deletes the newest capture (a track) and *frees* memory; no redo — per-track mute covers performance drop-outs |
| UI | Companion app for now; core designed for future phone app / screen |
| Architecture | Daisy-centric; app is a satellite |

---

## Hardware

- **Daisy Pod**: STM32H750 @ 480MHz, 64MB SDRAM, stereo audio I/O
- **Arturia KeyLab Essential 61**: keys → synth (ch 1), pads → drums
  (ch 10, notes 36–43), 9 encoders + 9 faders → parameters/mixer,
  transport buttons
- **Guitar** → Pod audio IN L (mono)
- **Speaker/PA** ← Pod audio OUT (stereo)
- **Companion device** ↔ Pod USB (CDC serial)
- KeyLab → Pod via UART TRS MIDI

```
guitar ──────────────┐
                     ▼
┌──────────┐ MIDI ┌──────────┐  USB CDC   ┌───────────────┐
│ KeyLab 61│ ────▶│ Daisy Pod│◀──────────▶│ Companion UI  │
└──────────┘      │ (brain)  │            │ (laptop now,  │
                  └────┬─────┘            │ phone later)  │
                       ▼                  └───────────────┘
                 speaker (stereo)
```

---

## The track model (core concept)

Everything loops against one **master clock** (96 PPQN, tap-tempo or encoder,
60–200 BPM). Each **loop track** has:

- **Content**: exactly one captured loop. Want to add more on top? Capture
  another track — there is no overdub and no layer stack. Because every
  track's playback position is `global_tick mod track_length`, same-length
  tracks are always phase-aligned, so a new track *is* a layer, with its own
  mixer strip for free.
- **Length**: 1, 2, 4, or 8 bars — independent per track, looping freely
  against the clock (1-bar hat groove under an 8-bar chord progression).
- **Mute/unmute**: instant, per track — the performance drop-out/in move.
- **Delete**: remove the track, freeing its memory (hold-to-delete gesture;
  not undoable — the gesture is the safety). **Undo is just a shortcut:
  delete the newest capture.** It frees memory, never costs any.
- **Level / pan / FX send**: mixer state per track.

### Track kinds (v2 target)

There is no fixed track list — every capture creates a track, and a track's
**kind** is simply the source it was captured from:

| Kind | Payload | Rendered by |
|---|---|---|
| Drum | MIDI notes | 8-voice sample drums (shared engine) |
| Synth | MIDI notes + CC automation | 6-voice polysynth (shared engine) |
| Audio | Mono audio | Looper playback |

A typical jam accumulates tracks: a 1-bar kick groove, a 1-bar hat pass, a
4-bar bassline, two 4-bar guitar loops, an 8-bar chord progression — each an
independent mixer strip. Deleting the newest is undo; muting any of them is
arrangement.

**Counts are memory-bound, not fixed.** Audio tracks draw from the SDRAM pool
(see *Audio looper*); MIDI tracks are near-free. A protocol/UI cap of 16
audio + 16 MIDI tracks keeps message formats and the mixer bounded; in
practice memory (audio) and taste (MIDI) are the real limits.

**Shared engines**: all synth tracks drive the one 6-voice synth and all drum
tracks the one 8-voice sampler, so total polyphony is shared — stack enough
dense synth tracks and voices steal. Mute is the pressure valve.

### Capture workflow (retrospective — the only record mode)

There is no record-arm, no count-in, no red light. Everything is always
being listened to; nothing is committed until you approve it after the fact.

1. The clock runs (drums or click establish the grid). All inputs are
   monitored through the mix and continuously recorded into **rolling
   buffers**: a small MIDI ring per source (notes + CCs) and one fixed
   8-bar audio ring for the guitar input.
2. Something gels → hit **Capture**. The last N bars are lifted from the
   selected source's rolling buffer, snapped to bar boundaries, and become a
   **new track** (memory permitting). No arming, no focusing, no "add track"
   step — capture *is* track creation.
3. The capture window ends at the bar boundary *nearest* the button press —
   pressing a beat late still grabs the phrase you meant.

**Choosing N**: per-source preset length (1/2/4/8), set from the app's
capture strip or the KeyLab Sampler-bank encoders 6/7. (The original
hold-Capture+encoder gesture is impossible on the KeyLab — its buttons
emit a single CC with no press/release pair; a Pod-encoder variant is a
candidate Phase 5 refinement.)

**Choosing the source** (as implemented 2026-07-05): the KeyLab **Live
button (CC 3)** is the one hardware capture trigger — the KeyLab
transport buttons exist only on its USB DAW port and never reach the
Pod's DIN input (daisy_hardware.md). **Single press = grab pads+keys**
(whichever played; silent sources skip quietly), **double press = grab
guitar**. The 400 ms double-press window costs nothing: the capture
window is cut at the FIRST press's clock position (retrospective capture
makes decision latency free). The app offers one-tap per-source buttons.

A bad take never becomes content — you just don't press the button. That is
what shrinks undo from a core loop into a rare corrective action.

**Early-downbeat grace** (Phase 4.1): a note played within a 32nd note
*before* the capture window's opening bar line was meant for that downbeat
— the extractor pulls it in and sits it exactly on the bar line (its
note-off travels by the same shift, so the duration survives). Always on,
independent of quantize; CCs and orphan note-offs get no grace. Without
this, an eager first hit simply vanished from the take — punishment.

Classic prospective punch-in falls out for free if ever wanted (press Capture
at the *end* of the passage instead of Record at the start); v2 ships
retrospective-only to keep a single mental model.

### Quantize (as built, Phase 4)

Optional input quantize for MIDI tracks: **off / light (50% toward nearest
16th) / hard 16th**, set per source (pads/keys). Applied **destructively at
capture commit** (groove.h) — the rolling ring keeps the raw performance
until the moment you grab, which is all the non-destructiveness the model
needs: a mis-quantized take costs one undo and a re-grab. Note-offs travel
by their note-on's delta so durations survive; the pairing is wrap-aware
across the loop seam; CC events are never quantized.

### Swing (as built, Phase 4)

Swing is a **playback-time tick warp**, never a mutation of stored events:
each 8th note is warped piecewise-linear (first 16th stretches, second
compresses; bar lines are fixed points), 50% straight to 75% full shuffle.
Because it's applied in the tick-mapping (seq_track.h), it is global,
non-destructive, and **live-tweakable while loops run** — from the app
slider or the Bank 4 "Swing" encoder. One groove for the whole box; audio
loops don't swing (no time-stretch on this CPU).

### Velocity taming (as built, Phase 4)

Optional power-curve compression (x^0.6) on drum-capture velocities —
lifts timid campfire pad hits without clipping the confident ones.
Applied at capture commit, toggleable, off by default.

### Tempo lock

MIDI loops replay at any tempo; audio loops cannot (no time-stretch on this
CPU). **Tempo locks once the first audio loop exists** and unlocks when all
audio tracks are cleared. The UI communicates this state.

---

## Audio engine

### Signal flow

```
                       ┌─ per-track level/pan ─┐
drum voices (×8) ─────▶│                       │
synth voices (×6) ────▶│        MIXER          │──▶ master ──▶ OUT L/R
audio loops (×3) ─────▶│                       │      ▲
guitar live in ───────▶│  (FX sends per chan)  │      │
                       └───────────┬───────────┘      │
                                   └──▶ reverb + delay┘ (shared sends)
```

- **Guitar monitoring** is straight-through in the audio callback:
  input → channel strip → mix. Latency ≈ one audio block (~1–2 ms). Guitar
  channel has level + FX send even when nothing is looped.
- **FX are core scope, not "later"**: one shared reverb (ReverbSc) + one
  tempo-synced delay, as send effects. Half the campfire vibe is the space.

### Sample drums

As currently built: 8 voices, one-shot samples in SDRAM, per-voice level /
pan / pitch / decay. Synthesized default kit at boot; sample upload from the
companion app and SD-card loading come later.

### Polysynth

As currently built: 6 voices × (2 PolyBLEP osc → SVF → dual ADSR), voice
stealing (oldest), CC-controlled parameters, filter-coefficient update
decimation (every 64 samples). **Monotimbral**: Synth A and Synth B tracks
play through the same live patch; CC automation per track provides
differentiation. (Multitimbral per-track patches are a possible later step —
they roughly halve polyphony per timbre or double CPU.)

### Audio looper

- Mono tracks captured from IN L, played back through the mixer
  (pan/level/sends give them stereo placement).
- **Dynamic track count, memory-pool backed.** All audio loop storage comes
  from one SDRAM pool. New tracks are created on demand; the number of tracks
  is whatever the pool can hold, not a compile-time constant (protocol/UI cap:
  16).
- **Bar-granule allocation.** Tempo locks when the first audio loop is
  captured, so `samples_per_bar` becomes a constant — the pool is carved into
  uniform 1-bar granules and a track is a chain of granules. This eliminates
  fragmentation: any freed bar is reusable by any future track, no compaction
  needed. Capture length is known at commit time, so allocation is exact —
  no tentative over-allocation.
- **Tracks are the content.** One captured loop per track, summed in the
  mixer like everything else. There are no shadow buffers and no layer
  stacks: every granule in use is music someone chose to keep.
- **Undo = delete the newest capture**, returning its granules to the pool.
  Undo *frees* memory rather than consuming it.
- **Merge/bounce** (K same-length tracks → one, reclaiming (K−1)/K of their
  memory at the cost of separate control) is the future memory valve if pool
  pressure ever demands it — deferred from v2, since the pool is large.
- The **capture ring** (9 bars at the slowest tempo — one bar of margin
  over the largest window so the CopyJob can never be lapped) is a fixed
  allocation outside the pool; it exists before tempo lock so the first
  capture works. Bar **anchors** {tick, sample_pos} recorded at every
  bar line make the tick↔sample mapping exact by construction.
- **Memory is a first-class, user-visible resource**, displayed in bars (the
  unit players think in): "23 bars of audio remaining." Slower tempos mean
  bigger bars and fewer of them; the gauge reflects that automatically.
- When the pool is exhausted: capture is refused with clear feedback; freeing
  memory = deleting tracks.

### Memory budget (64MB SDRAM)

Audio loops stored as 16-bit PCM, 48 kHz mono (inaudible loss in this
context, half the footprint of float; converted to float at playback).

| Region | Size |
|---|---|
| Drum sample bank (8 slots, float) | 16 MB |
| Audio capture ring (8 bars @ 60 BPM worst case) | ~3 MB |
| Reverb/delay lines, misc | ~2 MB |
| **Audio loop pool (everything else)** | **~42 MB** |

Pool capacity in bars depends on locked tempo (1 bar mono 16-bit =
`4 × 60/BPM × 48000 × 2` bytes; table corrected 2026-07-05 — the draft
had double-counted bytes/bar, verified by host test):

| Tempo | Bar size | Pool capacity |
|---|---|---|
| 60 BPM | 384 KB | ~114 bars |
| 120 BPM | 192 KB | ~229 bars |
| 180 BPM | 128 KB | ~344 bars |

Every bar in the pool is committed content — there is no undo/shadow
overhead. Even the worst case (60 BPM) holds e.g. twenty-eight 4-bar
audio tracks. The sample-bank/pool split is static in v2;
making it configurable (smaller kit → more loop time) is a later option.

### CPU budget

| Component | Est. |
|---|---|
| Polysynth (6 voices) | 30–40% |
| Sample drums (8 voices) | 5–10% |
| Audio looper (3 tracks + record) | ~5% |
| Reverb + delay | ~15% |
| Sequencer/transport/mixer | ~5% |
| **Total** | **~60–75%** |

Tight but workable. Escape hatches if needed: paraphonic synth (shared
filter), cheaper reverb, 4 synth voices.

---

## Transport & timing

- Master clock: 96 PPQN internal; tick generation in the audio callback for
  sample-accurate scheduling.
- **Tap tempo** (KeyLab button or app) + encoder fine adjust; 60–200 BPM.
- **Count-in / metronome**: optional audible click (mixed to master, level
  control) + LED beat flash. Essential for the guitarist to find the grid
  before drums exist.
- Play never needs to stop for: capturing, undoing, muting, deleting, tempo
  *display*, mixer moves, patch tweaks. Stop is for ending the song.
- Double-tap stop = stop + rewind (does **not** clear anything — clearing is
  always explicit and per-track).
- MIDI clock out (24 PPQN) + mirroring tracks to MIDI out: deferred, but the
  clock architecture should keep a clean "tick → subscribers" shape so it
  bolts on.

---

## CC automation (as built, Phase 4)

The v1 blend/offset system, generalized to the v2 capture model:

- 8 automatable targets (cutoff, resonance, filter env amount, amp ADSR,
  synth level), addressed by their **canonical Synth-bank CC numbers**
  (groove.h AUTO_CCS) so replay never depends on the active bank.
- CC moves ride in the same rolling MIDI ring as notes (thinned at record:
  ≥6 ticks and ≥2 values apart); a capture commits both together into the
  new track. A knob-only take makes a pure automation track — a captured
  filter sweep that modulates whatever you play live.
- Playback: `effective = recorded + (live_knob − base_at_COMMIT)`,
  clamped — live tweaks ride *on top of* recorded motion. Base is
  snapshotted per track at commit, which fixes v1's stale-base bug
  (base-at-play skewed every sweep by whatever the knob did in between).
- Plumbing honors the callback rules: recorded CCs dispatch from
  seq_track in the callback but detour through an SPSC ring back to the
  main loop, where parameter application lives (cc_map.h).
- A synth track bundles its notes and its CC motion; deleting the track
  removes both together. Lanes draw the motion as a faint dashed curve.

---

## Control surface (KeyLab, no screen)

Guiding rule: **performance-critical actions must be reachable without the
app**; the app mirrors and extends, never gatekeeps.

- **Keys** → synth (ch 1). **Pads** → drums (ch 10, notes 36–43).
- **Live button (CC 3)** → Capture: single press = pads+keys, double
  press = guitar. (KeyLab transport buttons never reach the DIN port —
  DAW-port only. Play/stop/rewind live on the Pod and in the app.)
- **Bank buttons (Part1/Part2/Live)** → switch encoder/fader bank:
  - Bank 1 *Mix*: track levels + pans + mutes (loop tracks + guitar live
    channel) — mute/unmute is the arrangement move, so it must be instant
  - Bank 2 *Synth*: cutoff, resonance, envelopes, osc params
  - Bank 3 *Drums*: per-voice pitch/decay/level
  - Bank 4 *FX/Loop*: reverb/delay sends, metronome level, capture source
    select + per-source length preset + undo / track delete
- **Fader pickup** (already built) prevents jumps on bank switch — keep.
- Capture source select / undo / delete need physical bindings in Bank 4
  (encoder-click or fader-as-button patterns TBD — iterate hands-on; the app
  always provides explicit buttons as the fallback). With tracks accumulating
  past 9 mixer strips, the Mix bank pages — newest tracks on the first page.
- Pod controls: button 1 play/stop, button 2 undo (delete newest capture),
  encoder = tempo / tap.

The exact bank layouts are an appendix-level detail to be tuned by playing,
not locked in the spec.

---

## Companion app

**Role: window and workbench, not brain.** Live it shows state; at home it
edits and manages.

- **Tabbed, arrange-first** (per hands-on feedback 2026-07-05: the flat
  panel stack was cluttered and redundant):
  - **Arrange** (primary): DAW-style track lanes showing actual captured
    content (mini drum-grid / piano-roll renderings from MSG_TRACK_DATA),
    per-lane looping playhead, inline mute/level, hold-to-delete;
    transport + capture strip with **per-source history rings** that
    visualize the rolling buffer filling ("4 bars banked, ready to grab")
    — the always-listening engine made visible; memory gauge (bars).
  - **Sound**: synth params + presets (+ drum params later).
  - **Mix**: the one mixer — track strips, guitar, engine strips, metro,
    master. No mixer state duplicated anywhere else.
  - **Debug**: MIDI monitor, raw log, protocol stats. Hidden by default.
- Content rendering is read-only in v2; **note editing** is a candidate
  post-v2 milestone (the groovebox pull), enabled by the same
  MSG_TRACK_DATA foundation — not an architecture change.
- Sample upload, patch/project save/load: later (workbench duties).
- **Portability**: keep `core/` (protocol, state, MIDI utils) free of DOM and
  WebSerial imports so it drops into a future React Native / Capacitor phone
  app or an embedded-screen UI. `serial/` is the swappable transport.

### Protocol (v2)

Same framing (`0xAA` sync, type, length, payload, checksum). Lessons from v1
(Step 14 bandwidth problems) baked in:

- **Event-driven, not 60fps-everything.** State changes push a message once;
  the app is the mirror of a stream of events.
- **Playhead is interpolated client-side** from tempo + a low-rate sync tick
  (~5–10 Hz), not streamed at 60fps.
- **Full-state snapshot on request/connect** (`REQ_STATE` → dump), so the app
  can cold-join mid-jam.
- New message families: track state (kind, length, mute), track
  create/destroy (all tracks are dynamic; undo = destroy of newest), capture
  commit events (window grabbed, resulting track), mixer state, audio levels
  (peak meters, low rate), tempo lock, audio pool status (bars used / free).

---

## Firmware architecture (rewrite guidance)

```
src/
├── main.cpp            # wiring only: init, callbacks, main loop
├── clock.h             # master clock, tick gen, tap tempo (audio-rate)
├── track.h             # dynamic track registry: kind, length, mute, delete
├── capture.h           # rolling rings (MIDI + audio), window extraction
├── seq_track.h         # MIDI payload: events, quantize, CC automation
├── audio_track.h       # audio payload: granule pool, chain playback
├── synth.h  sampler.h  # engines (salvage heavily from v1)
├── mixer.h  fx.h       # channel strips, sends, reverb/delay
├── midi_router.h       # one path for live + played-back MIDI (keep concept)
├── control_map.h       # KeyLab banks, pickup, focus/undo/clear bindings
├── protocol.h  ui_link.h # framing + event-driven state publishing
└── samples/
```

Hard rules:

- **Audio callback is real-time-safe**: no allocation, no printf, no USB
  writes. Callback ↔ main loop communicate via lock-free ring buffers
  (v1's playback-queue idea, made systematic).
- **One MIDI path**: live input and sequenced playback go through the same
  router (v1 got this right — keep it).
- **Engines are host-compilable**: clock math, event insertion, quantize,
  undo, and protocol framing must build off-target so they get unit tests
  (plain `make test` on the Mac). Only `main.cpp`/drivers touch libDaisy.
- **No hidden state coupling**: transport publishes ticks; tracks subscribe;
  engines render. UI publishing observes, never participates.

### Salvage list from v1

| Keep (concept and much code) | Rewrite | Drop |
|---|---|---|
| Synth engine + filter decimation trick | Sequencer (per-track lengths, undo) | Replace mode (undo supersedes it) |
| Sampler engine + synthesized kit | cc_map (new bank layout, focus/undo) | Double-click-stop-clears-all |
| Protocol framing + checksums | State publishing (event-driven) | 60fps TICK streaming |
| midi_router single-path idea | Transport (tick→subscriber shape, tap) | `note % 4` synth-track hashing |
| Fader pickup logic | App state handling (track model) | — |
| Companion `core/` separation | — | — |

---

## Open questions (non-blocking, flagged for later)

1. **Metronome routing** — no separate headphone out, so the click is in the
   main mix. Acceptable at a campfire? (Assumed yes for v2.)
2. **Synth multitimbrality** — per-track patches vs. shared live patch.
   v2: shared. Revisit if two-keyboard-sound jams feel limited.
3. **MIDI out / clock out** — deferred; architecture keeps the door open.
4. **Song sections** — per-track lengths cover polymeter, not verse/chorus.
   A "scene snapshot" concept could come later; out of scope for v2.
5. **Track navigation ergonomics** — a good jam yields 10–15 tracks; paging
   9 faders through them without a screen needs hands-on tuning (the app
   makes it trivial meanwhile). Merge/bounce of same-length tracks is the
   deferred memory/strip-count valve.
6. **Rolling-ring depth vs. max loop length** — both are 8 bars in v2; if
   hands-on play wants 16-bar guitar loops, the ring and the granule math
   scale together (memory cost roughly doubles per doubling).
7. **SD card** — sample/project persistence; after the core loop experience
   is right.
