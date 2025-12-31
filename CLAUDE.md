# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

GroovyDaisy is a Daisy Pod-based groove box with a companion React app. It features:
- 6-voice polyphonic synth triggered by MIDI keys (channel 1)
- 8-voice sample drum engine triggered by MIDI pads (notes 36-43 on channel 10)
- MIDI recording sequencer with overdub/replace modes
- CC automation with blend/offset playback mode
- Transport engine with PPQN-based timing (96 PPQN, 4-bar patterns)
- Bidirectional USB CDC communication with companion app at ~60fps
- MIDI input from Arturia KeyLab Essential 61 via UART

## Build Commands

```bash
# Build firmware (from this directory)
make

# Flash to Daisy via USB DFU
# First: hold BOOT, press RESET, release both
make program-dfu

# Run companion app
cd companion && npm install && npm run dev
# Open http://localhost:5173 in Chrome/Edge (requires WebSerial)
```

## Architecture

```
┌─────────────┐   UART MIDI    ┌─────────────┐    USB CDC      ┌─────────────┐
│  KeyLab 61  │ ──────────────>│  Daisy Pod  │<───────────────>│  React App  │
│  (control)  │  notes/CCs     │  (brain)    │  state @ 60fps  │  (UI/debug) │
└─────────────┘                └─────────────┘                 └─────────────┘
```

### Daisy Firmware Files

| File | Purpose |
|------|---------|
| `GroovyDaisy.cpp` | Main application - audio callback, MIDI handling, USB protocol |
| `protocol.h` | Binary message protocol (sync byte 0xAA, checksummed) |
| `transport.h` | Transport::Engine - play/stop/record, tempo, position tracking |
| `sampler.h` | Sampler::Engine - 8-voice drum playback with per-voice envelope |
| `synth.h` | Synth::Engine - 6-voice polyphonic synth with 2 osc, SVF filter, ADSR |
| `sequencer.h` | Sequencer::Engine - MIDI recording/playback, 8+4 tracks, overdub/replace modes |
| `automation.h` | Automation::Engine - CC automation recording/playback with blend mode |
| `cc_map.h` | CC to synth parameter mapping for KeyLab Essential 61 |
| `midi_router.h` | Unified MIDI routing for live input and sequencer playback |
| `samples/drums.h` | DrumSamples::SampleBank - synthesized drums generated into SDRAM |

### Companion App Files

| File | Purpose |
|------|---------|
| `src/App.tsx` | Main app - state management, protocol message handling |
| `src/core/protocol.ts` | ProtocolParser class - mirrors Daisy-side protocol.h |
| `src/serial/WebSerialPort.ts` | WebSerial API wrapper with auto-reconnect |
| `src/components/TransportBar.tsx` | Play/Stop/Record/Tempo controls |

### Protocol Messages

**Daisy -> Companion:**
- `0x01 TICK` - Position (4-byte tick count) sent ~60fps
- `0x02 TRANSPORT` - [playing, recording, bpm_lo, bpm_hi]
- `0x03 VOICES` - [synth_count, drum_count]
- `0x04 MIDI_IN` - [status, data1, data2] - forwarded MIDI events
- `0xFF DEBUG` - Text string for debugging

**Companion -> Daisy:**
- `0x80 PLAY`, `0x81 STOP`, `0x82 RECORD` - Transport commands
- `0x83 TEMPO` - [bpm_lo, bpm_hi]
- `0x90 REQ_STATE` - Request current state

## Testing

```bash
# Test protocol with Python script
python3 test_protocol.py

# Text commands via serial monitor (for basic testing)
# Send "PING" -> receive "PONG"
# Send "STATUS" -> receive current transport state
```

## Implementation Status

See PLAN.md for full roadmap. Currently implemented through Step 14:
- USB bidirectional communication
- Binary protocol with checksums
- Transport engine (play/stop/record/tempo)
- Sample drum engine (8 synthesized drums)
- MIDI input forwarding to companion (MIDI Monitor)
- Companion app with WebSerial, transport controls, MIDI monitor
- **MIDI recording sequencer** (8 drum + 4 synth tracks, overdub/replace modes)
- Double-click stop to clear pattern and reset
- **6-voice polyphonic synth** with 2 oscillators, SVF filter, dual ADSR envelopes
- **CC control** of synth parameters via KeyLab encoders/faders
- **CC automation** recording with blend/offset playback mode
- **CC Control Panel** with 4-bank system (General/Mix/Synth/Sampler)
- **Fader pickup mode** to prevent value jumps on bank switch

Next: Step 15 - Track Flattening (Audio Bounce)

**Track Flattening:** Render synth MIDI to audio buffers, enabling multiple distinct sounds (bass + chords) without multiplying CPU. Record → tweak preset → flatten → change preset → record more → flatten → all play as audio.

Note: MIDI Output and Sample Upload deferred to future steps
