# GroovyDaisy 🎸🔥

**A campfire jam box** — a groovebox + looper on a [Daisy Pod](https://electro-smith.com)
that several people play at once: KeyLab keys → synth, KeyLab pads →
drums, guitar → straight into the looper. A React companion app is the
visual window, never a requirement.

Built by **Cleo (human: ears, hands, guitar, button dances)** and
**Claude (AI: firmware, tools, and the occasional 3 AM lullaby)** —
pair-designing, pair-debugging, and jamming together on the result.

> The product sentence: *a fully featured groovebox with the jamability
> of a looper.*

## 🎵 Hear the AI's composition

[`tools/lullaby_for_the_bench.py`](tools/lullaby_for_the_bench.py) is a
complete performance Claude composed and played over the serial link at
the end of an all-night debugging session: Warm Pad chords captured
*with their filter sweep recorded into the loop*, a midnight-tuned kit,
a patch-morph to plucked lead with live knob rides blending over the
captured automation, a scene switch on the bar line for the outro, and
the reverb saying the last word. With a Pod flashed and plugged in:

```bash
python3 tools/lullaby_for_the_bench.py
```

[`tools/jam_bed.py`](tools/jam_bed.py) lays the same bones down as
living loops and hands the stage to a human with a guitar.

## What makes it feel good (the Stradivarius rules)

1. Musical time is circular — no display counts to infinity.
2. **Nothing punishes**: capture is retrospective (always-on rolling
   buffers — press *after* the good take), undo frees, a downbeat
   played a hair early is pulled onto the bar line instead of dropped.
3. The box listens even while you think — and shows it.
4. Latency of *decision* is free; latency of *sound* is sacred.
5. The screen is a window, never a requirement.

## Features (all hardware-verified)

- **Retrospective capture**: no record-arm, no count-in anxiety — grab
  the last 1/2/4/8 bars of pads, keys, or guitar after you play them.
  Every capture is a new track; undo deletes the newest.
- **Groove intelligence**: quantize at capture (off/light/hard,
  note-offs travel with their note-ons), live-tweakable swing
  (playback-time warp, non-destructive), velocity taming for wild pads.
- **CC automation**: knob motion rides the same rolling rings as notes;
  a captured synth track replays its filter sweeps, and live knob turns
  *blend on top* of the recorded motion.
- **The space**: shared reverb + tempo-synced ping-pong delay as send
  FX on every strip — including the live guitar. Captured loops inherit
  the space they were played in.
- **Note editing on lanes**: click drum-grid cells, drag piano-roll
  notes, shift-click to delete — loops are an instrument, not a tape.
- **Scenes**: eight mute-state snapshots switching on the next bar
  line. Verse/chorus at a campfire.
- **Giggability**: hands-free flashing over the serial link, a
  hard-fault flight recorder (crash → record PC → self-reboot in
  seconds), a livelock watchdog that names the wedged code section,
  per-strip meters + honest *peak* CPU at 10 Hz, WebSerial
  auto-reconnect.

## The all-night session (2026-07-06)

One night took this from "features built" to "instrument proven":
~30 hands-free flashes, five root-caused platform bugs (a pre-main
SDRAM constructor fault, a USB RX race that let corrupted frames
execute as random commands, denormals enabled in the audio interrupt
since day one, the CPU running at 400 MHz instead of 480, and QSPI XIP
starving the I-cache — fixed by moving the hot DSP into zero-wait
ITCM, 2.4× measured). The torture traffic that killed the box in
13 seconds now cruises at 59% peak CPU, and a 10-minute
everything-at-once soak passed with zero faults. The war stories live
in [daisy_hardware.md](daisy_hardware.md) and the commit log.

## Layout

```
src/            firmware (host-compilable headers + main.cpp wiring)
test/           host-side unit suite (make test — no ARM toolchain needed)
tools/          performances, hardware verifiers, flash + soak harnesses
companion/      React app (Chrome/Edge WebSerial)
SPEC.md         the authoritative design
ROADMAP.md      the forward plan + the horizon
```

## Build

```bash
make            # firmware (pinned libDaisy v5.4.0 — see daisy_hardware.md)
make test       # 104 host-side tests
python3 tools/flash.py          # hands-free flash (firmware ≥ v5)
cd companion && npm i && npm run dev   # app at localhost:5173
```

## Hardware

Daisy Pod (STM32H750, 64 MB SDRAM) · Arturia KeyLab Essential 61 (DIN
MIDI) · guitar into audio in · speaker/headphones out · USB to the
companion app (both Pod and Seed ports work).

---

*Made with love, solder, and one very long night by a human and an AI
who wanted a campfire that fits in a box.*
