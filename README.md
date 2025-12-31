# GroovyDaisy

A groove box built on the [Daisy Pod](https://electro-smith.com/products/pod) with a companion React app for control and visualization.

## Features

- **6-voice polyphonic synth** - 2 oscillators, state-variable filter, dual ADSR envelopes
- **8-voice drum sampler** - Synthesized drums generated at startup
- **MIDI recording sequencer** - 4-bar patterns, 96 PPQN resolution, overdub/replace modes
- **CC automation** - Record knob/fader movements with blend/offset playback
- **Companion app** - React-based UI with WebSerial for transport control, MIDI monitoring, and CC visualization

## Hardware

- [Daisy Pod](https://electro-smith.com/products/pod) - STM32H750 @ 480MHz, 64MB SDRAM
- [Arturia KeyLab Essential 61 mk2](https://www.arturia.com/products/hybrid-synths/keylab-essential-61-mk3) (or similar MIDI controller)
- TRS MIDI cable connecting KeyLab to Daisy Pod UART (pin D14)

## Build & Flash

Requires the [DaisyExamples](https://github.com/electro-smith/DaisyExamples) environment with libDaisy and DaisySP built.

```bash
# Build firmware
make

# Flash via USB (hold BOOT, press RESET, release both, then run)
make program-dfu
```

## Companion App

Requires Node.js and a browser with WebSerial support (Chrome/Edge).

```bash
cd companion
npm install
npm run dev
```

Open http://localhost:5173 and click "Connect" to link with the Daisy Pod via USB serial.

## Architecture

```
┌─────────────┐   UART MIDI    ┌─────────────┐    USB CDC      ┌─────────────┐
│  KeyLab 61  │ ──────────────>│  Daisy Pod  │<───────────────>│  React App  │
│  (control)  │  notes/CCs     │  (brain)    │  state @ 60fps  │  (UI/debug) │
└─────────────┘                └─────────────┘                 └─────────────┘
```

The Daisy Pod runs all audio synthesis, sequencing, and MIDI processing. The companion app provides the primary UI since the Pod has no display.

## Project Structure

| File | Description |
|------|-------------|
| `GroovyDaisy.cpp` | Main firmware - audio callback, MIDI handling, USB protocol |
| `synth.h` | 6-voice polyphonic synthesizer engine |
| `sampler.h` | 8-voice drum sample playback engine |
| `sequencer.h` | MIDI recording/playback (8 drum + 4 synth tracks) |
| `automation.h` | CC automation recording with blend mode |
| `transport.h` | Play/stop/record, tempo, position tracking |
| `protocol.h` | Binary message protocol for USB communication |
| `companion/` | React app source |

## License

MIT
