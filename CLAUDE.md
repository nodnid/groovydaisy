# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

GroovyDaisy is a **campfire jam box**: a Daisy Pod groovebox + looper played
by several people at once (KeyLab keys → synth, KeyLab pads → drums, guitar →
audio input/looper), with a React companion app as the visual window.

**SPEC.md is the authoritative design; ROADMAP.md is the forward plan**
(phases 4–6 + horizon + feel principles). daisy_hardware.md holds the
Seed/Pod/bootloader facts and toolchain gotchas. PLAN.md is the historical
v1 roadmap (superseded). v1 is preserved at git tag `v1-final` and, with
its full history and extra WIP (arrange UI, freeze), on branch
`v1-history`.

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

# Flash (Daisy bootloader is installed; app runs from QSPI):
#   press RESET, then press BOOT while the Seed LED "breathes"
make program-dfu
# (One-time bootloader install on a fresh Pod: BOOT+RESET, make program-boot)

# Host-side unit tests (no ARM toolchain needed, runs on macOS)
make test

# Companion app
cd companion && npm install && npm run dev
# Open http://localhost:5173 in Chrome/Edge (requires WebSerial)
```

Toolchain: **pinned to libDaisy v5.4.0** at `../DaisyExamples2/{libDaisy,DaisySP}`
(a space-free symlink to "DaisyExamples 2"). Do NOT switch to the newer
v7.x checkout — its USB middleware breaks CDC enumeration on macOS
(A/B-verified; details in daisy_hardware.md).

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

v2 rewrite: Phases 0–3 built; 0–2.5 hardware-verified (2026-07-05).
Phase 3 (guitar audio looper: granule pool, tempo lock, CopyJob,
waveform lanes, Live double-press = grab guitar) and the
play-resumes-from-bar-top fix are committed and NOT yet verified with a
guitar on hardware — that verification is the next action, then Phase 4
per ROADMAP.md (quantize, swing, CC-automation capture).
