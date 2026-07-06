# GroovyDaisy — Roadmap to the Ultimate Daisy Groovebox

Written 2026-07-05 by Claude (with the keys — see the creative mandate:
own the feel decisions, discuss genuine scope forks). SPEC.md holds the
design; this holds the sequence and the ambition. The product sentence:
**a fully featured groovebox with the jamability of a looper.**

## State as of this writing

Phases 0–3 hardware-verified. Phase 4 (groove) flashed AND
hardware-verified by Claude over the serial link (tools/verify_groove.py:
quantize OFF/HARD/LIGHT, note-off travel, early-downbeat grace — all
measured on device 2026-07-05). **Phase 5 (the sound opens up) is built,
host-tested and committed — flash + feel-test is the next action**: the
reverb defaults, the dotted-8th delay, drum pitch/decay ranges, and CPU
headroom with loops + FX running.

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

## Phase 5 — The sound opens up (BUILT 2026-07-05, awaiting feel-test)

- ✅ Reverb (ReverbSc, SDRAM) + tempo-synced ping-pong delay as send FX;
  mono post-fader sends on every strip incl. guitar; gentle reverb ON by
  default; captured tracks inherit their source strip's sends. Delay
  divisions in 16ths, dotted-8th default, ~50 ms slew on tempo change.
- ✅ Per-strip peak meters + master L/R + CPU % at 10 Hz (MSG_METERS);
  meters on every companion mixer strip, CPU in the Mix tab.
- ✅ Bank *Live* finalized: FX/swing/capture-lens on encoders, masters +
  sends on faders. Mod-wheel/CC1 collision fixed (lone-zero + 400 ms
  guard — wheel wiggles never switch banks).
- ✅ Drum sound design: Bank *Drums+* = per-voice pitch (±1 octave) +
  decay (30 ms–3 s). Per-voice filter deferred to the horizon.
- Protocol v4 (CMD_FX/MSG_FX/MSG_METERS). Feel-test list: reverb size
  sweet spot, dotted-8th feedback level, CPU headroom with 3 audio
  loops + reverb, drum pitch/decay ranges.

## Phase 6 — Giggability (in progress)

- ✅ Ring-drop stats surfaced: MSG_STATS 0x23 (midi/cc drops + TX laps),
  sent on change ≤1 Hz; companion shows a warning block only when
  nonzero. All-zero and silent is the healthy state.
- ✅ WebSerial auto-reconnect + re-hydrate: the dead reconnectTimer
  finally lives — unexpected disconnect polls getPorts() (grants
  persist, no gesture needed), 'connect' event fast-path, reopen fires
  onConnect → CMD_REQ_STATE re-hydrates. Single-grant fast path makes
  "Connect" one click.
- ✅ Capture window boundary edge tests (half-open [start, end)).
- tools/verify_fx.py ready for the next flash: FX round-trip, 10 Hz
  meters + CPU headroom, reverb tail + ping-pong echoes measured via
  the meter stream, mod-wheel guard, zero ring drops.
- Remaining: snapshot audit (golden bytes for the full snapshot burst),
  30-min soak on hardware, undo-during-copy / play-stop-spam sweeps,
  file the libDaisy v7 macOS CDC issue upstream with our A/B repro.

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
