# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

GroovyDaisy is a **campfire jam box**: a Daisy Pod groovebox + looper played
by several people at once (KeyLab keys → synth, KeyLab pads → drums, guitar →
audio input/looper), with a React companion app as the visual window.

**SPEC.md is the authoritative design document.** PLAN.md is the historical
v1 roadmap (superseded). The v2 rewrite is in progress; v1 is preserved at
git tag `v1-final`.

Core v2 concepts (see SPEC.md for detail):
- Retrospective capture: always-on rolling buffers; Capture grabs the last
  N bars — no record-arm, no count-in
- Every capture creates a new track (no layers/overdub); undo = delete
  newest capture; per-track mute is the arrangement move
- Dynamic audio tracks from a bar-granule SDRAM pool; tempo locks while
  audio loops exist
- Event-driven USB protocol (no 60fps streaming); playhead interpolated
  client-side

## Build Commands

```bash
# Build firmware (from repo root)
make

# Flash to Daisy via USB DFU
# First: hold BOOT, press RESET, release both
make program-dfu

# Host-side unit tests (no ARM toolchain needed, runs on macOS)
make test

# Companion app
cd companion && npm install && npm run dev
# Open http://localhost:5173 in Chrome/Edge (requires WebSerial)
```

Toolchain: libDaisy/DaisySP are expected at `../DaisyExamples/{libDaisy,DaisySP}`
(override with `make LIBDAISY_DIR=... DAISYSP_DIR=...`).

**FLASH budget warning:** v1 already uses ~95% of the 128 KB internal flash.
v2 will need the Daisy bootloader (`APP_TYPE=BOOT_QSPI`) — plan for it.

## Layout

```
src/            # firmware (main.cpp + headers; headers are host-compilable
                #   except where they need DaisySP — see SPEC.md rules)
test/           # host-side unit tests (mini_test.h harness, make test)
tools/          # test_protocol.py serial monitor
companion/      # React app (core/ is portable, serial/ is WebSerial-specific)
```

## Architecture (v2 target)

```
guitar ──────────────┐
                     ▼
┌──────────┐ MIDI ┌──────────┐  USB CDC   ┌───────────────┐
│ KeyLab 61│ ────▶│ Daisy Pod│◀──────────▶│ Companion app │
└──────────┘      │ (brain)  │            └───────────────┘
                  └────┬─────┘
                       ▼
                 speaker (stereo)
```

Hard rules for firmware code:
- Audio callback is real-time safe: no allocation, no printf, no USB writes
- Callback ↔ main loop only via SpscRing (src/rt_queue.h)
- One MIDI path: live input and sequenced playback go through midi_router
- Headers in src/ take memory via Init(ptr, size); main.cpp owns the
  DSY_SDRAM_BSS statics (libDaisy does NOT zero SDRAM BSS — init explicitly)

## Testing

- `make test` — host-side unit suite (protocol, clock math, and growing
  per-phase: rings, granule pool, capture windows, tempo lock)
- `python3 tools/test_protocol.py` — live serial monitor against the Pod
- Hardware verification scripts per phase are in the implementation plan

## Status

v2 rewrite Phase 0 complete (git + restructure + host tests).
Implementation follows the 7-phase plan; see SPEC.md for the design.
