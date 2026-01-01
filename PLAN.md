# Daisy Pod Groove Box - Implementation Plan

## Design Decisions (Confirmed)

- Architecture: Daisy-centric (sequencer + engines on Daisy)
- Companion app: PRIMARY UI (React), essential for debugging and control
- Synth: 6-voice polysynth + separate mono bass and chord tracks
- Drums: Sample-based (8 tracks, lighter CPU)
- Sequencer: MIDI recorder style (4-8 bars), not fixed step grid
- Recording: Overdub + Replace modes, selectable
- Automation: CC recording with blend/offset mode (live tweaks add/subtract)
- MIDI Clock: Internal master, send clock out
- MIDI Output: Mirror all tracks to MIDI out for external gear
- Effects: Implement later, possibly only on frozen/flattened tracks

## Hardware

- Daisy Pod: STM32H750 @ 480MHz, 64MB SDRAM, stereo audio I/O
- Arturia KeyLab Essential 61 mk2: 61 keys, 8 pads, 9 encoders, 9 faders, transport
- MIDI Connection: KeyLab -> Pod UART (TRS MIDI pin D14)
- Companion Connection: USB CDC (serial) over Pod's micro-USB

---

## Architecture

```
┌─────────────┐   UART MIDI    ┌─────────────┐    USB CDC      ┌─────────────┐
│  KeyLab 61  │ ──────────────▶│  Daisy Pod  │◀───────────────▶│  React App  │
│  (control)  │  notes/CCs     │  (brain)    │  state @ 60fps  │  (UI/debug) │
└─────────────┘                └──────┬──────┘                 └─────────────┘
                                      │
                               ┌──────┴──────┐
                               │  Audio Out  │
                               │  MIDI Out   │ ──▶ External gear
                               └─────────────┘
```

**Daisy responsibilities:**
- MIDI recording sequencer (4-8 bars per pattern)
- 6-voice polysynth (2 tracks: mono bass + poly chords)
- 8-track sample drum machine
- CC automation recording + playback with blend/offset
- MIDI input (KeyLab) + MIDI output (external gear)
- State streaming to companion @ 60fps
- Internal tempo master, send MIDI clock out

**Companion app responsibilities:**
- Primary UI (Pod has no screen)
- Debug monitoring: MIDI input, audio engine state, sequencer
- Transport controls
- Pattern visualization + editing (later)
- Sample upload via serial
- Project save/load

---

## CPU Budget Estimate

| Component    | Voices | Est. CPU |
|--------------|--------|----------|
| Polysynth    | 6      | ~30-40%  |
| Sample drums | 8      | ~5-10%   |
| Reverb       | 1      | ~10%     |
| Sequencer    | -      | ~2%      |
| Total        |        | ~50-60%  |

Comfortable headroom for future expansion.

---

## Drum Machine (Sample-Based)

### Architecture

```cpp
struct DrumVoice {
    float* sample_data;      // Pointer into SDRAM
    size_t sample_length;    // Length in samples
    size_t play_head;        // Current position
    float  amplitude;        // Envelope level
    float  decay;            // Envelope decay rate
    float  pitch;            // Playback rate (1.0 = normal)
    float  level;            // Track volume
    bool   playing;          // Is voice active
};

DrumVoice drums[8];  // 8 drum tracks
```

### Per-voice parameters (controllable)

- Level: Track volume (0.0-1.0)
- Pitch: Playback speed (0.5-2.0)
- Decay: Amplitude envelope decay
- Pan: Stereo position
- Filter: Simple lowpass per-voice (optional)

### Sample storage

- SDRAM: `float DSY_SDRAM_BSS sample_bank[48000 * 60]` (~60 sec mono)
- Divided into 8 slots of ~7.5 sec each
- Loaded from SD card on boot or via companion software

---

## Polysynth Engine (6-voice)

### Architecture

```cpp
struct SynthVoice {
    Oscillator osc1, osc2;
    Svf filter;
    Adsr amp_env, filt_env;
    uint8_t note;
    uint8_t velocity;
    bool active;
    uint32_t start_time;  // For voice stealing
};

SynthVoice voices[6];

// Shared (paraphonic option)
// Svf shared_filter;  // If CPU is tight
```

### Parameters

| Category   | Parameters                                  |
|------------|---------------------------------------------|
| Osc 1      | Waveform, Level, Detune                     |
| Osc 2      | Waveform, Level, Detune, Octave             |
| Filter     | Cutoff, Resonance, Env Amount, Key Track    |
| Amp Env    | Attack, Decay, Sustain, Release             |
| Filter Env | Attack, Decay, Sustain, Release             |
| LFO        | Rate, Depth, Destination (filter/pitch/amp) |

### Voice allocation

- Find free voice, or steal oldest note
- Velocity scales amplitude and filter envelope amount
- Portamento option (glide between notes)

---

## Sequencer Engine (MIDI Recorder Style)

### Data structures

```cpp
#define NUM_PATTERNS 16
#define MAX_EVENTS_PER_TRACK 512
#define DRUM_TRACKS 8
#define SYNTH_TRACKS 2  // Bass (mono) + Chords (poly)
#define PPQN 96         // Pulses per quarter note

// MIDI event with timestamp
struct MidiEvent {
    uint32_t tick;       // Position in PPQN ticks
    uint8_t  type;       // NOTE_ON, NOTE_OFF, CC
    uint8_t  data1;      // Note number or CC number
    uint8_t  data2;      // Velocity or CC value
};

// CC automation point
struct AutomationPoint {
    uint32_t tick;
    uint8_t  cc;         // Which CC
    uint8_t  value;      // 0-127
};

// Track modes
enum TrackMode {
    TRACK_MIDI,          // MIDI events, triggers engine
    TRACK_AUDIO          // Frozen/recorded audio
};

struct Track {
    TrackMode mode;
    MidiEvent events[MAX_EVENTS_PER_TRACK];
    uint16_t event_count;
    AutomationPoint automation[256];
    uint16_t auto_count;
    // For audio mode
    float* audio_buffer;
    size_t audio_length;
};

struct Pattern {
    Track drums[DRUM_TRACKS];
    Track synth[SYNTH_TRACKS];  // [0]=bass, [1]=chords
    uint32_t length_ticks;      // Pattern length (4-8 bars)
    uint8_t swing;              // 0-100%
};

Pattern patterns[NUM_PATTERNS];
```

### Recording Modes

- **Overdub:** New events layer on existing (OR mode)
- **Replace:** Recording erases existing events
- Toggle via transport button or companion app

### Automation Blend/Offset

```cpp
// When playing back automation with live override:
float GetCCValue(uint8_t cc) {
    float recorded = GetRecordedCC(cc, current_tick);
    float live_offset = GetLiveOffset(cc);  // From current knob position
    return fclamp(recorded + live_offset, 0.0f, 127.0f);
}
```

### Timing

- Internal clock at PPQN resolution
- 60-200 BPM range
- Send MIDI clock out (24 PPQN standard)
- Pattern lengths: 4 bars (1536 ticks) or 8 bars (3072 ticks) at 96 PPQN

---

## Companion Software (React App)

### Tech Stack

- Framework: React 18+ with Vite
- Styling: CSS-in-JS or Tailwind (dark theme)
- Communication: WebSerial API (Chrome/Edge)
- UI Components:
  - react-dial-knob or react-rotary-knob - Rotary controls
  - @radix-ui/react-slider - Faders
  - framer-motion - Animations
  - wavesurfer-react - Waveform display (later)

### App Structure

```
companion/
├── src/
│   ├── core/                 # Platform-agnostic (for future mobile)
│   │   ├── protocol.ts       # Message encoding/decoding
│   │   ├── state.ts          # App state management
│   │   └── midi-utils.ts     # MIDI parsing helpers
│   ├── serial/
│   │   └── WebSerialPort.ts  # WebSerial wrapper
│   ├── components/
│   │   ├── Layout.tsx        # Main app layout
│   │   ├── ConnectionStatus.tsx
│   │   ├── TransportBar.tsx  # Play/Stop/Record/Tempo
│   │   ├── MidiMonitor.tsx   # MIDI input log
│   │   ├── EngineState.tsx   # Voice activity, CPU
│   │   ├── SequencerView.tsx # Playhead, pattern
│   │   └── DebugLog.tsx      # Raw message log
│   ├── App.tsx
│   └── main.tsx
├── index.html
├── package.json
└── vite.config.ts
```

### MVP Features (Debug-Focused)

1. **Connection:** Auto-connect on load, reconnect on disconnect
2. **MIDI Monitor:** Parsed human-readable (Note C4 vel=100, CC 74=50)
3. **Engine State:** Active voices, CPU load estimate
4. **Sequencer State:** Playhead position, current pattern, playing/stopped
5. **Transport Controls:** Play, Stop, Record toggle, Tempo adjust
6. **Raw Log:** Toggle to see raw serial messages

### UI Layout (Dark Theme)

```
┌─────────────────────────────────────────────────────────────┐
│ [●] Connected to Daisy Pod          ▶ PLAY  ■ STOP  ● REC │
├───────────────────────┬─────────────────────────────────────┤
│   MIDI INPUT          │         ENGINE STATE                │
│   ───────────         │         ────────────                │
│   Note On  C4  v=100  │   Synth Voices: ●●●○○○ (3/6)       │
│   CC 74 = 64          │   Drum Voices:  ●●○○○○○○ (2/8)     │
│   Note Off C4         │   CPU Load: ▓▓▓▓▓░░░░░ 52%         │
│   CC 71 = 80          │                                     │
│                       │   Tempo: 120 BPM                    │
│                       │   Pattern: 1/16                     │
│                       │   Position: Bar 2, Beat 3          │
├───────────────────────┴─────────────────────────────────────┤
│   SEQUENCER TIMELINE                                        │
│   ═══════●════════════════════════════════════════════════ │
│   Bar 1        Bar 2        Bar 3        Bar 4             │
├─────────────────────────────────────────────────────────────┤
│   RAW LOG (toggle)                                          │
│   > STATE:POS:1:2:45                                        │
│   > STATE:VOICE:S:3                                         │
│   < CMD:TEMPO:125                                           │
└─────────────────────────────────────────────────────────────┘
```

### Communication Protocol (Binary for efficiency)

**Daisy → App (60fps state updates)**

Message format: `[TYPE:1][LENGTH:2][PAYLOAD:N][CHECKSUM:1]`

Types:
- `0x01` TICK - Playhead position (pattern, bar, beat, tick)
- `0x02` TRANSPORT - Playing/stopped/recording state
- `0x03` VOICES - Active voice count (synth + drums)
- `0x04` MIDI_IN - Echo of received MIDI event
- `0x05` CC_STATE - Current CC values (for UI sync)
- `0x10` PATTERN - Full pattern data dump (on request)
- `0x11` SAMPLE - Sample slot info
- `0xFF` DEBUG - Debug text message

**App → Daisy (commands)**

Types:
- `0x80` CMD_PLAY
- `0x81` CMD_STOP
- `0x82` CMD_RECORD
- `0x83` CMD_TEMPO `[BPM:2]`
- `0x84` CMD_PATTERN `[NUM:1]`
- `0x90` REQ_STATE - Request full state dump
- `0x91` REQ_PATTERN `[NUM:1]` - Request pattern data
- `0xA0` SAMPLE_START `[SLOT:1][SIZE:4]` - Begin sample upload
- `0xA1` SAMPLE_DATA `[CHUNK:N]` - Sample data chunk
- `0xA2` SAMPLE_END - Finish sample upload

### State Management

```typescript
interface AppState {
  connected: boolean;
  transport: 'stopped' | 'playing' | 'recording';
  tempo: number;
  pattern: number;
  position: { bar: number; beat: number; tick: number };
  voices: { synth: number; drums: number };
  cpuLoad: number;
  midiLog: MidiEvent[];
  rawLog: string[];
}
```

---

## KeyLab Essential 61 Mapping

### Hardware Reference

**9 Encoders (endless, L→R):**
| Position | CC | Position | CC |
|----------|-----|----------|-----|
| Enc 1 | 74 | Enc 6 | 18 |
| Enc 2 | 71 | Enc 7 | 19 |
| Enc 3 | 76 | Enc 8 | 16 |
| Enc 4 | 77 | Enc 9 | 17 |
| Enc 5 | 93 | | |

**9 Faders (fixed position, L→R):**
| Position | CC | Position | CC |
|----------|-----|----------|-----|
| Fader 1 | 73 | Fader 6 | 81 |
| Fader 2 | 75 | Fader 7 | 82 |
| Fader 3 | 79 | Fader 8 | 83 |
| Fader 4 | 72 | Fader 9 | 85 |
| Fader 5 | 80 | | |

**Bank Switch Buttons:**
| Button | CC | Function |
|--------|-----|----------|
| Part 1 / Next | 1 | Next bank |
| Part 2 / Prev | 2 | Previous bank |
| Live / Bank | 3 | (reserved) |

**Other Controls:**
- Pads: Channel 10, notes 36-43
- Mod wheel: CC 1 (when not in bank mode)
- Transport: Standard MMC

### 4-Bank CC System

Bank switching via CC 1 (Next) and CC 2 (Prev) buttons.

**Bank 1: General (Master Controls)**
| Fader | CC | Parameter |
|-------|-----|-----------|
| 1 | 73 | Drum Master Level |
| 2 | 75 | Synth Master Level |
| 7 | 82 | Velocity → Amp |
| 8 | 83 | Velocity → Filter |
| 9 | 85 | Master Output |

**Bank 2: Mix (Levels + Pan)**
| Enc | CC | Parameter | Fader | CC | Parameter |
|-----|-----|-----------|-------|-----|-----------|
| 1 | 74 | Drum 1 Pan | 1 | 73 | Drum 1 Level |
| 2 | 71 | Drum 2 Pan | 2 | 75 | Drum 2 Level |
| 3 | 76 | Drum 3 Pan | 3 | 79 | Drum 3 Level |
| 4 | 77 | Drum 4 Pan | 4 | 72 | Drum 4 Level |
| 5 | 93 | Drum 5 Pan | 5 | 80 | Drum 5 Level |
| 6 | 18 | Drum 6 Pan | 6 | 81 | Drum 6 Level |
| 7 | 19 | Drum 7 Pan | 7 | 82 | Drum 7 Level |
| 8 | 16 | Drum 8 Pan | 8 | 83 | Drum 8 Level |
| 9 | 17 | Synth Pan | 9 | 85 | Synth Level |

**Bank 3: Synth (Sound Design)**
| Enc | CC | Parameter | Fader | CC | Parameter |
|-----|-----|-----------|-------|-----|-----------|
| 1 | 74 | Filter Cutoff | 1 | 73 | Osc1 Level |
| 2 | 71 | Filter Env Attack | 2 | 75 | Osc2 Level |
| 3 | 76 | Filter Env Decay | 3 | 79 | Filter Resonance |
| 4 | 77 | Osc2 Detune | 4 | 72 | Filter Env Amount |
| 5 | 93 | Amp Attack | 5 | 80 | Amp Sustain |
| 6 | 18 | Amp Decay | 6 | 81 | Filter Env Sustain |
| 7 | 19 | Amp Release | 7 | 82 | Filter Env Release |
| 8 | 16 | Osc1 Waveform | 8 | 83 | (unused) |
| 9 | 17 | Osc2 Waveform | 9 | 85 | Synth Level |

**Bank 4: Sampler (Future)**
| Enc | CC | Parameter |
|-----|-----|-----------|
| 1 | 74 | Drum Pitch |
| 2 | 71 | Drum Decay |
| 3 | 76 | Drum Filter |
| 4 | 77 | Drum Filter Res |
| 5 | 93 | Swing Amount |

### Transport

| Control | Function |
|---------|----------|
| Play | Start sequencer |
| Stop | Stop (2x = reset to step 1) |
| Record | Toggle record mode |
| Loop | Toggle pattern loop |
| << >> | Previous/next pattern |

---

## Memory Allocation

### SDRAM (64MB total)

```cpp
// In global scope
float DSY_SDRAM_BSS sample_bank[48000 * 60];    // 48MB - drum samples
float DSY_SDRAM_BSS audio_buffer[48000 * 40];   // 14MB - audio recording (~2.5 min stereo)
// Remaining ~2MB for reverb tails, delay lines, etc.
```

### Sample Slots

- 8 drum slots × ~6 seconds each (mono 48kHz float)
- Loaded from: hardcoded C arrays initially, SD card later, companion upload

---

## Implementation Phases

### Phase 1: Companion App + Basic Daisy Communication

**Goal:** Working React app that connects to Daisy and shows debug info

**Companion tasks:**
1. Vite + React project setup
2. WebSerial connection (auto-connect, reconnect)
3. Binary protocol parser
4. Basic UI: connection status, raw log
5. Transport controls (send commands)

**Daisy tasks:**
1. USB CDC initialization (bidirectional)
2. Binary protocol encoder
3. State broadcast at 60fps (transport, position, placeholder data)
4. Command parser (play/stop/tempo)
5. Basic MIDI input echo to companion

**Files to create:**
- `companion/` - Full React project
- `pod/GrooveBox/GrooveBox.cpp` - Main file
- `pod/GrooveBox/protocol.h` - Serial protocol
- `pod/GrooveBox/Makefile`

### Phase 2: Sample Drum Machine

**Goal:** Playable 8-track sample drums via KeyLab pads

**Daisy tasks:**
1. Sample playback engine (8 voices)
2. Hardcoded sample data (kick, snare, hat, etc.)
3. MIDI note input (pads trigger drums)
4. Per-voice parameters (level, pitch, decay)
5. Drum voice state to companion

**Companion tasks:**
- Drum voice activity display
- Level meters (later)

### Phase 3: MIDI Recording Sequencer

**Goal:** Record and playback MIDI with automation

**Daisy tasks:**
1. MIDI event recording (with timestamps)
2. CC automation recording
3. Playback engine with event scheduling
4. Overdub/replace mode toggle
5. Pattern data to companion

**Companion tasks:**
- Playhead timeline visualization
- Pattern/bar display
- Record mode indicator

### Phase 4: Polysynth Engine

**Goal:** 6-voice subtractive synth on bass + chord tracks

**Daisy tasks:**
1. Voice structure (2 osc + filter + 2 env)
2. Voice allocation with stealing
3. MIDI routing (keys → synth, pads → drums)
4. CC mapping for synth parameters
5. Synth voice state to companion

**Companion tasks:**
- Synth parameter panel (knobs, ADSR visualizer)

### Phase 5: Full Integration

**Goal:** Complete workflow with MIDI out, effects, polish

**Daisy tasks:**
1. MIDI clock output (24 PPQN)
2. MIDI note output (mirror tracks)
3. Master reverb (ReverbSc)
4. Swing/shuffle
5. CC blend/offset mode

**Companion tasks:**
- Pattern editing UI
- Sample upload via serial
- Project save/load

### Phase 6: SD Card & Standalone (Future)

**Goal:** Full standalone operation

**Tasks:**
1. Load samples from SD card
2. Save/load patterns to SD
3. Pod LED feedback
4. Boot without companion

---

## File Structure

```
pod/GroovyDaisy/
├── GroovyDaisy.cpp       # Main application
├── Makefile
├── sampler.h           # Sample playback engine
├── synth.h             # Polysynth engine
├── sequencer.h         # MIDI recorder/sequencer
├── midi_handler.h      # MIDI routing (KeyLab + output)
├── protocol.h          # USB serial protocol
├── automation.h        # CC automation with blend/offset
└── samples/            # Default samples as C arrays
    ├── kick.h
    ├── snare.h
    ├── hihat.h
    └── ...

companion/
├── src/
│   ├── core/           # Platform-agnostic logic
│   ├── serial/         # WebSerial wrapper
│   ├── components/     # React components
│   ├── App.tsx
│   └── main.tsx
├── index.html
├── package.json
├── vite.config.ts
└── tailwind.config.js
```

---

## Project Name

**GroovyDaisy** - Daisy-powered groove box

---

## Incremental Implementation Steps

Each step is self-contained and testable. Start fresh context for each.

---

### Step 1: Daisy USB CDC "Hello World" ✅ COMPLETE

**Goal:** Daisy sends text over USB, viewable in serial monitor

**Create files:**
- `pod/GroovyDaisy/GroovyDaisy.cpp`
- `pod/GroovyDaisy/Makefile`

**Implementation:**
1. Initialize DaisyPod
2. Initialize USB CDC (hw.seed.usb_handle)
3. In main loop: send "Hello from GroovyDaisy\n" every 500ms
4. Blink LED to show it's running

**Test:**
- Flash to Daisy
- Open serial monitor (Arduino IDE, screen, or `cat /dev/tty.usbmodem*`)
- See "Hello from GroovyDaisy" repeating
- LED blinks

---

### Step 2: Bidirectional USB Communication ✅ COMPLETE

**Goal:** Daisy receives commands and responds

**Modify:** `pod/GroovyDaisy/GroovyDaisy.cpp`

**Implementation:**
1. Set up USB receive callback
2. Echo received bytes back with prefix "ECHO: "
3. Recognize "PING" command, respond with "PONG"
4. LED changes color on received data

**Test:**
- Send "PING" from serial monitor
- Receive "PONG" back
- Send any text, see it echoed
- LED flashes on receive

---

### Step 3: Binary Protocol - Daisy Side ✅ COMPLETE

**Goal:** Structured binary messages from Daisy

**Create:** `pod/GroovyDaisy/protocol.h`
**Modify:** `pod/GroovyDaisy/GroovyDaisy.cpp`

**Implementation:**
1. Define message format: `[0xAA][TYPE][LEN][PAYLOAD][CHECKSUM]`
2. Implement SendMessage(type, payload, len)
3. Send TICK message (type 0x01) with counter every 16ms (~60fps)
4. Send DEBUG message (type 0xFF) for text

**Test:**
- View raw bytes in hex (use xxd or hex viewer)
- See 0xAA header appearing regularly
- Counter incrementing in payload

---

### Step 4: React App Scaffold ✅ COMPLETE

**Goal:** Basic React app with Vite, dark theme

**Create:** `companion/` directory with full Vite React TypeScript setup

**Implementation:**
1. `npm create vite@latest companion -- --template react-ts`
2. Add Tailwind CSS (dark theme)
3. Create basic layout: header, main panels
4. Add "Connect" button (non-functional placeholder)
5. Add placeholder panels: "MIDI Monitor", "Engine State"

**Test:**
- `cd companion && npm install && npm run dev`
- Open http://localhost:5173
- See dark-themed UI with panels
- Responsive layout

---

### Step 5: WebSerial Connection ✅ COMPLETE

**Goal:** React app connects to Daisy, shows connection status

**Modify:** `companion/src/`

**Implementation:**
1. Create `serial/WebSerialPort.ts` - connection wrapper
2. Add connect/disconnect logic
3. Show connection status in header
4. Display raw received bytes in log panel
5. Auto-reconnect on disconnect

**Test:**
- Click Connect, select Daisy from browser popup
- See "Connected" status
- See raw hex bytes streaming (from Step 3)
- Unplug USB, see "Disconnected", replug, auto-reconnects

---

### Step 6: Protocol Parser in React ✅ COMPLETE

**Goal:** Parse binary messages, display structured data

**Create:** `companion/src/core/protocol.ts`
**Modify:** `companion/src/components/`

**Implementation:**
1. Parse message format `[0xAA][TYPE][LEN][PAYLOAD][CHECKSUM]`
2. Validate checksum
3. Dispatch by message type
4. Display TICK counter in UI
5. Display DEBUG text messages

**Test:**
- See tick counter updating at ~60fps
- Add `hw.PrintLine()` on Daisy, see text in DEBUG panel

---

### Step 7: MIDI Input + Monitor ✅ COMPLETE

**Goal:** Receive MIDI from KeyLab, display human-readable in companion

**Daisy - Modify:** `pod/GroovyDaisy/GroovyDaisy.cpp`

**Daisy Implementation:**
1. Initialize UART MIDI (hw.midi)
2. Poll MIDI in main loop
3. Parse Note On/Off, CC messages
4. Send MIDI_IN message (type 0x04) to companion
5. LED reacts to MIDI (flash on note)

**Companion - Create:** `companion/src/core/midi-utils.ts`
**Companion - Modify:** `companion/src/components/MidiMonitor.tsx`

**Companion Implementation:**
1. Parse MIDI_IN message payload (status, data1, data2)
2. Convert note numbers to names (C4, F#3, etc.)
3. Show CC numbers and values
4. Scrolling log with timestamps
5. Filter by type (Notes, CCs, All)

**Test:**
- Connect KeyLab via MIDI cable
- Press keys/pads, see LED flash on Daisy
- See "Note On C4 vel=100" in companion MIDI Monitor
- Turn encoders, see "CC 74 = 64"
- Filter works correctly

---

### Step 8: Transport State ✅ COMPLETE

**Goal:** Play/Stop/BPM state on Daisy, controllable from companion

**Created:** `pod/GroovyDaisy/transport.h`
**Modified:** `pod/GroovyDaisy/GroovyDaisy.cpp`
**Created:** `companion/src/components/TransportBar.tsx`

**Implementation:**
1. Transport::Engine class with PPQN-based timing (96 PPQN)
2. Position tracking: bar, beat, tick within pattern
3. Play/Stop/Record state machine
4. Tempo control (30-300 BPM)
5. Send TRANSPORT message (type 0x02) on state change
6. Parse CMD_PLAY/STOP/RECORD/TEMPO from companion
7. Pod button controls: Button1=Play/Stop (double-click=reset), Button2=Record
8. Pod encoder: Adjust tempo
9. LED1 shows transport state with beat pulse (green=play, red=record, blue=stopped)

**Companion:**
- TransportBar component with Play/Stop/Record buttons
- Tempo display with +/- buttons and click-to-edit
- Position display (bar.beat) synchronized with playhead
- Progress bar showing position in 4-bar pattern (no CSS transitions for sync)
- Bar markers with vertical dividers and playhead indicator
- All timing displays use same pre-wrapped tick value for consistency

**Bug fixes applied:**
- Fixed timing desync between bar.beat display, progress bar, and playhead
- Pre-wrap tick value with modulo before calculating bar/beat
- Removed CSS transitions from progress bar to prevent visual lag
- Added vertical dividers between bar markers

**Test:**
- Click Play in app, Daisy starts counting, LED pulses green on beat
- Click Stop, Daisy stops, LED goes dim blue
- Click Record, LED pulses red
- Change tempo with +/- or click to edit, see position update faster/slower
- Pod Button1 toggles play/stop (double-click resets to bar 1)
- Pod Button2 toggles record mode
- Pod encoder adjusts tempo
- Verify bar.beat, progress bar, and playhead all show same position

---

### Step 9: Sample Drum Engine ✅ COMPLETE

**Goal:** Trigger drum samples from pads, show voice activity in companion

**Created:** `pod/GroovyDaisy/sampler.h`
**Created:** `pod/GroovyDaisy/samples/drums.h`
**Modified:** `pod/GroovyDaisy/GroovyDaisy.cpp`
**Modified:** `companion/src/App.tsx`, `companion/src/components/EngineState.tsx`

**Daisy Implementation:**
1. DrumVoice struct (sample pointer, playhead, amplitude envelope, pitch, level, velocity)
2. Sampler::Engine class with 8 voice instances
3. Synthesized samples generated at runtime into SDRAM (~100KB total):
   - Pad 1: Kick (sine + pitch sweep)
   - Pad 2: Snare (sine body + noise)
   - Pad 3: HH Closed (short noise)
   - Pad 4: HH Open (longer noise)
   - Pad 5: Clap (multi-burst noise)
   - Pad 6: Tom Low
   - Pad 7: Tom Mid
   - Pad 8: Rim (click)
4. Trigger on MIDI channel 10, notes 36-43 (KeyLab pads)
5. Mix drum output to stereo audio (mono samples, summed to both channels)
6. Send VOICES message (type 0x03) when active count changes

**Companion:**
- App.tsx tracks voiceState { synth, drums }
- EngineState accepts synthVoices/drumVoices props
- Drum voice dots light up green when voices are active

**Test:**
- Hit KeyLab pads, hear drum sounds
- Multiple simultaneous voices work
- Retriggering works (same pad while playing)
- See voice dots light up in companion app

---

### Step 10: MIDI Recording & Playback ✅ COMPLETE

**Goal:** Record pad hits with timing, play back recorded patterns

**Created:** `pod/GroovyDaisy/sequencer.h`
**Created:** `pod/GroovyDaisy/midi_router.h`
**Modified:** `pod/GroovyDaisy/GroovyDaisy.cpp`

**Daisy Implementation:**
1. MidiEvent struct (tick, status, data1, data2)
2. Track struct with 512 events per track, playback_index optimization
3. Sequencer::Engine with 8 drum tracks + 4 synth tracks (12 total)
4. Recording: sorted insertion by tick position
5. Playback: efficient scanning with per-track playback index
6. **Overdub mode:** Layer new events on existing
7. **Replace mode:** Clear track on first note of recording pass
8. Encoder click toggles overdub/replace mode
9. Double-click stop clears pattern and resets position
10. Pattern loops at 4 bars (1536 ticks at 96 PPQN)

**Unified MIDI Router (added later):**
- `midi_router.h` - Central routing class for all MIDI events
- All events (live + sequenced) flow through same path
- Routes to sampler (ch 10), synth (ch 1), and MIDI Monitor
- Playback queue for audio callback → main loop communication
- Both NoteOn and NoteOff events now visible in MIDI Monitor during playback
- Synth notes recorded to 4 synth tracks (hashed by note % 4)

**Companion:**
- TransportBar shows playhead position and bar markers
- MIDI Monitor shows drum AND synth triggers (both live and playback)

**Test:**
- Press Record, play pattern on pads → events stored
- Press Stop → playback stops
- Press Play → hear recorded pattern loop
- Click encoder → toggle "Mode: Overdub" / "Mode: Replace"
- Overdub: record more notes → both old and new play
- Replace: record → only new notes play (old cleared on first hit)
- Double-click stop → pattern cleared, position reset
- Record synth notes → NoteOn/NoteOff recorded and played back
- Playback shows both NoteOn and NoteOff in MIDI Monitor ✓

---

### Step 11: Polysynth Engine ✅ COMPLETE

**Goal:** 6-voice polyphonic synth responding to keys

**Created:** `pod/GroovyDaisy/synth.h`

**Daisy Implementation:**
1. SynthVoice struct (2 osc, SVF filter, 2 ADSR envs)
2. Array of 6 SynthVoice instances
3. Initialize with DaisySP modules (PolyBLEP oscillators, state-variable filter)
4. FindFreeVoice() - find inactive or steal oldest
5. Note tracking (which voice plays which note)
6. Note on → trigger voice with velocity
7. Note off → release envelope
8. 4 factory presets (Init Patch, Warm Pad, Pluck Lead, Bass)
9. Send synth voice count to companion
10. Voice diagnostics: NaN detection, stuck voice timeout (3s), envelope tracking
11. **CPU Optimization:** Filter SetFreq/SetRes called every 64 samples (~750Hz) instead of per-sample (48kHz) - reduces coefficient recalculation by 64x
12. **AllNotesOff():** Release all voices on transport stop (prevents stuck notes)

**Companion:**
- EngineState shows synth voice dots
- SynthPanel with all parameter controls (osc, filter, envelopes, velocity)
- PresetManager for factory preset selection
- Protocol support for synth parameter messages

**Key optimization learned:**
- Filter coefficient calculation (SetFreq/SetRes) is expensive
- PolyAnalog example updates filter params in main loop (~30Hz)
- GroovyDaisy uses FILTER_UPDATE_RATE=64 for ~750Hz updates
- Envelopes still processed every sample for smooth modulation

**Test:**
- Play keys on KeyLab, hear synth ✓
- Notes sustain while held ✓
- Release envelope on key up ✓
- Play chords, hear all notes ✓
- Play 6+ notes, oldest voice stolen ✓
- CPU stays stable with 6 voices ✓
- See synth voice dots in companion ✓

---

### Step 12: Synth CC Control ✅ COMPLETE

**Goal:** KeyLab encoders/faders control synth

**Created:** `pod/GroovyDaisy/cc_map.h`
**Modified:** `pod/GroovyDaisy/midi_router.h`, `pod/GroovyDaisy/GroovyDaisy.cpp`

**Daisy Implementation:**
1. Full CC to parameter mapping in `cc_map.h`:
   - Encoders: Filter cutoff (74), resonance (71), osc waveforms (76, 77), ADSR (93, 18, 19, 16)
   - Faders: Osc levels (73, 75), filter env (79), synth level (85)
   - Mod wheel (CC 1) adds to filter cutoff
2. Helper functions: CCToFreq (log 20-20kHz), CCToTime (log 0.001-5s), CCToNorm (0-1)
3. `HandleSynthCC()` applies CC values to synth engine
4. MIDI router routes channel 1 CCs through HandleSynthCC
5. Synth state sent to companion via MSG_SYNTH_STATE on param change

**Companion:**
- MidiMonitor shows CC messages with cc number and value (`74 = 64`)
- CC_NAMES dictionary provides friendly names for KeyLab controls
- SynthPanel shows all synth parameters, updated from SYNTH_STATE messages

**Test:**
- Turn KeyLab encoders, hear synth change ✓
- CC values appear in MIDI Monitor with names ✓
- SynthPanel shows updated parameter values ✓

---

### Step 13: CC Automation ✅ COMPLETE

**Goal:** Record CC changes with timing, blend live tweaks with recorded values

**Created:** `pod/GroovyDaisy/automation.h`
**Modified:** `pod/GroovyDaisy/GroovyDaisy.cpp`

**Daisy Implementation:**
1. AutoPoint struct (tick, value) - CC number implicit from track
2. AutoTrack with up to 256 points per CC, playback index for efficient scanning
3. 8 automatable CCs: Filter cutoff (74), resonance (71), ADSR (93/18/19/16), filter env amt (79), synth level (85)
4. Thinning: MIN_RECORD_INTERVAL=6 ticks, MIN_VALUE_CHANGE=2 to avoid redundant points
5. Base values captured on playback start for blend calculation
6. Blend/offset mode: effective = recorded + (current - base), clamped to 0-127
7. Integration with transport: base capture on play, reset on stop, clear on double-click reset
8. Diagnostic output shows automation point count

**Automation::Engine API:**
- `RecordCC(tick, cc, value)` - Record with thinning
- `UpdateCurrentValue(cc, value)` - Track knob position for blend
- `CaptureBaseValues()` - Snapshot for blend mode
- `Process(tick, callback)` - Playback with blend applied
- `Clear()` / `ResetPlayback()` - Pattern management

**Test:**
- Record while moving filter cutoff → points recorded with thinning ✓
- Play back, hear filter movement ✓
- During playback, turn knob → hear blend offset combined ✓
- Debug output shows "Auto: N pts, Blend: ON" ✓

---

### Step 14: CC Control Panel with Multi-Bank System ✅ COMPLETE

**Goal:** Graphical CC panel in companion with 4-bank system, fader pickup mode

**Daisy Firmware:**
1. Rewrite `cc_map.h` with 4-bank system (General/Mix/Synth/Sampler) ✓
2. Handle CC 1 (Next) and CC 2 (Prev) for bank switching ✓
   - Fixed: KeyLab sends value=0 on button press (not 127), removed value check
3. Add fader pickup logic (prevent value jumps on bank change) ✓
4. Add to `sampler.h`: drum_pan[8], drum_master_level ✓
5. Add to `synth.h`: synth_pan, synth_master_level ✓
6. Add protocol messages: MSG_CC_BANK, MSG_FADER_STATE, MSG_MIXER_STATE ✓
7. Route CCs through bank-aware handler in GroovyDaisy.cpp ✓
8. Forward CC events to companion via MSG_MIDI_IN ✓

**Companion App:**
1. Create `ccMappings.ts` - bank/parameter definitions ✓
2. Create `CCControlPanel.tsx` - 4 tabs with encoder/fader rows ✓
3. Create `CCEncoder.tsx` - visual knob with CC/param labels ✓
4. Create `CCFader.tsx` - visual fader with pickup indicators ✓
5. Update protocol.ts to parse new message types ✓
6. Update App.tsx with bank state and fader states ✓
7. **Key fix:** UI updates from CC events (MSG_MIDI_IN) instead of periodic state messages ✓

**Fader Pickup Mode:**
- Pickup tolerance: ±3 CC values
- On bank switch: faders need pickup (yellow border)
- Once fader crosses param value: tracking (green border)
- Ghost marker shows target value when not picked up

**Architecture Decision - CC-Driven UI Updates:**
Originally planned periodic MSG_MIXER_STATE/MSG_SYNTH_STATE messages for UI sync.
However, USB CDC bandwidth limitations caused inconsistent delivery.

**Solution:** UI now updates directly from CC events:
- CC events flow reliably via MSG_MIDI_IN
- On CC receive, companion looks up target parameter via bank mappings
- Updates mixerState or synthParams directly from CC value
- Immediate, responsive UI updates without periodic polling

**Files modified:**
- `cc_map.h` - 4-bank CC routing with HandleBankSwitch() fix
- `GroovyDaisy.cpp` - CC forwarding, bank change detection after MIDI processing
- `App.tsx` - CC-to-state mapping in MSG_MIDI_IN handler
- `ccMappings.ts` - Bank definitions and ParamTarget enum
- `CCControlPanel.tsx`, `CCEncoder.tsx`, `CCFader.tsx` - UI components

**Test:**
- Bank switching with CC 1/2 (single press works) ✓
- UI tabs sync with hardware bank selection ✓
- Encoders/faders control correct params per bank ✓
- **UI knobs/faders update immediately when hardware moved** ✓
- Fader pickup works (no value jumps on bank switch) ✓
- Drum pans affect stereo positioning ✓
- Drum/synth master levels work correctly ✓
- MIDI Monitor shows all CC events ✓

---

### Step 15: Track Flattening (Audio Bounce)

**Goal:** Render synth MIDI tracks to audio buffers, enabling multiple distinct sounds without multiplying CPU cost

**Rationale:**
- Single 6-voice synth uses ~35% CPU
- Two synths would push to ~70%, leaving no headroom
- Flattening trades CPU for memory (abundant: 64 MB SDRAM)
- Matches DAW workflow (Ableton Freeze, Logic Bounce in Place)
- Enables: record bass → flatten → change preset → record chords → flatten → both play as audio

**Create:** `pod/GroovyDaisy/audio_track.h`
**Modify:** `pod/GroovyDaisy/GroovyDaisy.cpp`, `pod/GroovyDaisy/sequencer.h`, `pod/GroovyDaisy/protocol.h`

**Constraints:**
- Maximum pattern length: **8 bars** (no 16-bar patterns)
- Maximum frozen tracks: **3** (leave memory for future features)
- Tempo/bar count locked while any track is frozen

**Memory Budget:**

| BPM | 4 bars | 8 bars |
|-----|--------|--------|
| 60 | 5.86 MB | 11.72 MB |
| 90 | 3.91 MB | 7.81 MB |
| 120 | 2.93 MB | 5.86 MB |
| 150 | 2.34 MB | 4.69 MB |

Worst case (3 tracks × 8 bars @ 60 BPM):
- Drums: ~8 MB
- 3 frozen tracks: ~35 MB
- **Total: ~43 MB**
- **Reserved for future: ~21 MB** (custom samples, effects, resampling, etc.)

Typical case (3 tracks × 4 bars @ 120 BPM):
- Total: ~17 MB
- Available: ~47 MB

**Future: Resample to Combine Tracks**
When all 3 slots are used, user can resample/bounce multiple frozen tracks into one, freeing slots for more layers.

**Daisy Implementation:**

1. **AudioTrack struct:**
```cpp
struct AudioTrack {
    float* buffer_L;           // Pointer into SDRAM
    float* buffer_R;
    size_t length;             // Buffer length in samples (varies by tempo/bars)
    size_t playhead;           // Current playback position
    bool is_flattened;         // true = play audio, false = play MIDI through synth
    bool is_rendering;         // true during flatten operation
};
```

2. **SDRAM allocation:**
```cpp
// 3 audio track buffers (stereo, 32 seconds max each @ 48kHz)
// 32 sec = 8 bars @ 60 BPM worst case
constexpr size_t MAX_AUDIO_TRACK_SAMPLES = 48000 * 32;  // 32 seconds
float DSY_SDRAM_BSS audio_track_L[3][MAX_AUDIO_TRACK_SAMPLES];
float DSY_SDRAM_BSS audio_track_R[3][MAX_AUDIO_TRACK_SAMPLES];
// Total: ~35 MB reserved for frozen tracks
```

3. **Flatten operation:**
   - User triggers flatten (companion button or Pod control)
   - Set track.is_rendering = true
   - Play pattern once from start
   - Route synth output to track buffer instead of (or in addition to) main out
   - On pattern loop: is_rendering = false, is_flattened = true
   - Track now plays audio on subsequent loops

4. **Playback modes in audio callback:**
```cpp
if(track.is_flattened) {
    // Read from audio buffer (cheap)
    out_L += track.buffer_L[playhead];
    out_R += track.buffer_R[playhead];
} else {
    // Process through synth engine (expensive)
    out_L += synth.Process();
}
```

5. **Unflatten operation:**
   - Set is_flattened = false
   - Track reverts to MIDI playback through synth
   - Audio buffer preserved until next flatten

**Protocol Messages:**
- `CMD_FLATTEN_TRACK [track_num]` - Start flatten operation
- `CMD_UNFLATTEN_TRACK [track_num]` - Revert to MIDI mode
- `MSG_TRACK_STATE [track_num, is_flattened, is_rendering]` - Track status

**Companion App:**
- Track status indicator (MIDI / Rendering... / Audio)
- Flatten/Unflatten buttons per synth track
- Visual feedback during render (progress or "Rendering...")

**Workflow Example:**
1. Record bass line on synth (MIDI) → Track 1
2. Tweak synth preset for bass sound
3. Click "Flatten" → pattern plays once, audio captured
4. Track 1 now shows "Audio" status, tempo locked
5. Change synth preset to pad sound
6. Record chord progression (MIDI) → Track 2
7. Click "Flatten" on Track 2
8. Change preset to lead sound
9. Record melody (MIDI) → Track 3
10. Click "Flatten" on Track 3
11. All 3 tracks play as audio with distinct sounds
12. Total CPU: ~10% (just sample playback)
13. Want more layers? Resample tracks together to free slots (future feature)

**Test:**
- Record 4-bar synth pattern
- Flatten track → verify audio plays back identically
- Try to change tempo → verify locked (shows warning)
- Change synth preset → verify flattened track sound unchanged
- Unflatten → verify MIDI plays through synth again, tempo unlocked
- Flatten all 3 tracks → verify all play together
- Try to flatten 4th → verify blocked (max 3 frozen)
- Test at different tempos: 60, 120, 180 BPM
- Test at 4 and 8 bar patterns

---

### Step 16: Sample Upload via Companion

**Goal:** Load custom samples from companion app

**Modify:** `pod/GroovyDaisy/protocol.h`, `pod/GroovyDaisy/GroovyDaisy.cpp`

**Companion:**
- Drag-drop WAV file
- Convert to float samples (mono, 48kHz)
- Chunk and send via serial (USB CDC)
- Progress indicator

**Daisy:**
- Receive SAMPLE_START, SAMPLE_DATA, SAMPLE_END
- Write to SDRAM sample slot
- Validate and acknowledge chunks

**Test:**
- Drag WAV file to app
- See upload progress
- Hear new sample when hitting pad

---

## Further Steps (Future)

### MIDI Output (Hardware Required)

Mirror tracks to MIDI out for external gear. **Deferred** - requires TRS MIDI output hardware (not present on Daisy Pod).

**Hardware options when ready:**
1. **DIY TRS MIDI OUT circuit** - 2 resistors + TRS jack connected to UART TX
2. **USB MIDI mode** - Replaces USB CDC (loses companion app, or requires composite USB)
3. **WebMIDI bridge** - Companion app forwards MIDI via browser WebMIDI API

**Implementation (when hardware available):**
- Queue pattern for MIDI output (audio callback → main loop)
- Drum triggers → Note On (channel 10)
- Synth notes → Note On/Off (channel 1)
- MIDI clock (24 PPQN from internal 96 PPQN)
- Start/Stop/Continue transport messages
- Optional: MIDI Thru (live input echo), CC automation output

### Other Future Features

- Master reverb
- Pattern editing in companion
- Project save/load
- SD card loading
- Synth track in sequencer
