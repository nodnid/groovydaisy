# GroovyDaisy — Roadmap to the Ultimate Daisy Groovebox

Written 2026-07-05 by Claude (with the keys — see the creative mandate:
own the feel decisions, discuss genuine scope forks). SPEC.md holds the
design; this holds the sequence and the ambition. The product sentence:
**a fully featured groovebox with the jamability of a looper.**

## State as of this writing

Phases 0–3 built and hardware-verified (2026-07-05), including guitar
capture and USB MIDI tier 1. **Phase 4 (groove intelligence) is built,
host-tested and committed — flash + feel-test is the next action**:
does light quantize feel invisible, does 62% swing sit right, does a
captured filter sweep + live knob ride feel like one gesture?

Hard-won platform truths live in daisy_hardware.md and the memory files:
libDaisy pinned to v5.4.0 (v7 breaks macOS CDC), BOOT_QSPI bootloader
flash flow, per-port TX cursor queue (never single-tail two CDC ports),
KeyLab transport buttons never reach DIN (Live button CC 3 = capture).

## Feel principles (the Stradivarius rules)

1. Musical time is circular. No display counts to infinity.
2. Nothing punishes: capture is retrospective, undo frees, gestures are
   the safety. The player should never fear a button.
3. The box listens even when you think. Make the listening visible.
4. Latency of DECISION is free (retrospective); latency of SOUND is
   sacred (callback discipline).
5. The screen is a window, never a requirement.

## Phase 4 — Groove intelligence (BUILT 2026-07-05, awaiting feel-test)

MIDI capture became musical (all in src/groove.h + seq_track playback,
protocol v3, companion Groove panel; SPEC.md has the as-built sections):
- ✅ Quantize at capture: off / light (50% toward nearest 16th) / hard,
  per source. Note-offs travel with their note-ons, wrap-aware.
- ✅ **Swing** (deferred from v1!): playback tick-mapping warp —
  non-destructive, live-tweakable (app slider + Bank 4 encoder 5).
- ✅ CC automation capture: 8 canonical synth CCs ride the keys ring
  (thinned), replay through the main loop, blend base at commit.
  Lanes render the motion as a dashed curve.
- ✅ Velocity taming: optional x^0.6 compress on pad captures.

## Phase 5 — The sound opens up

- Reverb (ReverbSc in the reserved SDRAM region) + tempo-synced delay
  as send FX; guitar live strip gets sends (space without loops).
- Per-strip peak meters (accumulate in callback, publish 10 Hz) + CPU
  load for real in the Debug tab.
- KeyLab Bank 4 finalized: sends, FX params, metronome level, capture
  lengths. Fix the mod-wheel/CC1 bank-switch collision (move bank
  switching off CC 1 or debounce-guard it).
- Drum sound design (the v1 Sampler bank promise): per-voice pitch/
  decay/filter — the arrange view's drum lanes make this satisfying.

## Phase 6 — Giggability

- Snapshot audit (golden bytes), ring-drop stats surfaced, 30-min soak.
- WebSerial auto-reconnect + re-hydrate (the dead reconnectTimer
  finally lives). Edge sweeps per the plan (capture at boundary ±1,
  undo during copy, play/stop spam).
- File the libDaisy v7 macOS CDC issue upstream with our A/B repro.

## The horizon (what makes it *ultimate*)

Ordered by feel-per-effort, informed by Cleo's reactions so far:

1. **Note editing on lanes** (the groovebox pull, explicitly wanted):
   click a drum-grid cell to toggle, drag piano-roll notes. Foundation
   (MSG_TRACK_DATA) already ships; needs CMD_TRACK_EDIT + in-place
   event patching (respect the never-mutate-active-payload rule: edit =
   clone-to-reserved-slot + atomic swap, same as capture).
2. **Scenes** (song sections): snapshot mute-states + switch on the
   next bar line. Cheap to build, huge arrangement power. Verse/chorus
   at a campfire.
3. **Merge/bounce**: same-length tracks → one (frees strips and, for
   audio, granules). v1-history's freeze concept generalized: bounce
   MIDI tracks THROUGH the synth into audio granules to free polyphony
   — this is also how multitimbrality happens on one synth engine.
4. **Session persistence** (SD card): save/load the whole jam —
   tracks, granules, mixer, patch. The Pod has full 4-bit SDMMC; the
   bootloader already proves the slot works. A jam you can reopen
   tomorrow changes what the instrument *is*.
5. **Sample import** (SD or serial): drop-in drum kits. samples/ dir
   convention + existing Sampler engine slots.
6. **Overdub-into-track as an option** (revisit): capture-creates-track
   stays the default, but a "capture into focused track" modifier may
   earn its place once editing exists. Discuss before building (scope
   fork).
7. **Phone app**: core/ is already DOM-free by design; wrap in
   Capacitor, WebSerial→Web Bluetooth or USB-C serial. The campfire
   loses the laptop.
8. **USB MIDI, tier 2** (tier 1 SHIPPED 2026-07-05: CMD_MIDI_INJECT +
   the companion's Web MIDI bridge — any Mac MIDI source, IAC bus, DAW,
   or script plays the box through the serial link, and bridged playing
   is capture-able like everything else). Tier 2 = class-compliant USB
   MIDI device so the Daisy appears in every DAW with zero glue: our
   pinned libDaisy v5.4 has MidiUsbTransport with per-port selection,
   but the USB device CLASS is a single global (`usbd_mode` in
   src/hid/usb.cpp) — CDC-on-Pod + MIDI-on-Seed needs a small patch to
   the pinned checkout. Contained, but it touches the hard-won USB
   stack: schedule deliberately, keep the patch in-repo.
9. **MIDI out** (expansion header UART — the TRS jack is input-only,
   encoder click owns the TX pin): sync external gear to the jam.
9. **Stretch: audio input FX** (amp-ish drive/comp on the guitar strip)
   and **stereo audio capture** (line-in R is currently unused).

## Working agreement

Build → host tests green → commit → Cleo flashes (RESET → BOOT during
breathe → `make program-dfu`) → Cleo plays and reports feel → fix or
advance. Update SPEC.md as decisions land; update this file as the
horizon shifts.
