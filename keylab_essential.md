# Arturia KeyLab Essential 61 mk2 — Complete MIDI, MCU, LED & LCD Reference
Concise reference sheet intended for use by AI coding models.
Covers: MIDI CCs, Note mappings, ports, MCU/HUI messages, jog wheel behavior, LED/LCD SysEx, pad color IDs.
No examples, no implementation suggestions, no device-specific instructions.

---

## 1. PORTS

### 1.1 USB MIDI Endpoints
- **Main Port**
  - Keys, pads, encoders, faders, mod wheel, pitch bend, sustain pedal, user-map buttons.
  - Mirrored to 5-pin DIN OUT.

- **DAW Port**
  - MCU/HUI messages.
  - Receives SysEx for LEDs and LCD.
  - Used for transport, navigation, DAW-command buttons, jog wheel.

### 1.2 MIDI Channels
- Main port: Channel 1 (default) for CCs/keys.
- Pads: Channel 10 (default).
- MCU/HUI uses its own message layer on DAW port.

---

## 2. MODES

### 2.1 Map Select
- Pad 1: Analog Lab (fixed)
- Pad 2: DAW Map (MCU/HUI, fixed)
- Pad 3–8: User Maps 1–6 (editable)

### 2.2 General Behavior
- Analog Lab & DAW maps: fixed internal assignments.
- User maps: assignable for most pads/encoders/faders/buttons.

---

## 3. MAIN PORT MIDI MAPPINGS

### 3.1 Keys & Performance
- Keys: Note On/Off, velocity 0–127
- Pitch bend: standard 14-bit
- Mod wheel: CC 1
- Sustain pedal: CC 64 (switch mode default)

### 3.2 Pads (factory)
| Pad | Note | MIDI # | Channel |
|-----|------|-------:|--------:|
| 1 | C1 | 36 | 10 |
| 2 | C♯1 | 37 | 10 |
| 3 | D1 | 38 | 10 |
| 4 | D♯1 | 39 | 10 |
| 5 | E1 | 40 | 10 |
| 6 | F1 | 41 | 10 |
| 7 | F♯1 | 42 | 10 |
| 8 | G1 | 43 | 10 |

Pads can be reassigned in User maps.

---

## 4. ENCODERS & FADERS (Main Port)

### 4.1 Encoders (endless)
| Enc | CC |
|-----|---:|
| 1 | 74 |
| 2 | 71 |
| 3 | 76 |
| 4 | 77 |
| 5 | 93 |
| 6 | 18 |
| 7 | 19 |
| 8 | 16 |
| 9 | 17 |

### 4.2 Faders
| Fader | CC |
|--------|---:|
| 1 | 73 |
| 2 | 75 |
| 3 | 79 |
| 4 | 72 |
| 5 | 80 |
| 6 | 81 |
| 7 | 82 |
| 8 | 83 |
| 9 | 85 |

---

## 5. DAW PORT — MCU/HUI REFERENCE

### 5.1 MCU Transport Buttons
| Function | Note | MIDI # |
|----------|------|-------:|
| REWIND | G6 | 91 |
| FAST FWD | G♯6 | 92 |
| STOP | A6 | 93 |
| PLAY | A♯6 | 94 |
| RECORD | B6 | 95 |
| CURSOR UP | C7 | 96 |
| CURSOR DOWN | C♯7 | 97 |
| SCRUB | D7 | 98 |
| ZOOM | D♯7 | 99 |
| CURSOR LEFT | E7 | 100 |
| CURSOR RIGHT | F7 | 101 |

### 5.2 MCU Channel Controls
(8 channels; standard MCU note layout)

- REC ARM 1–8
- SOLO 1–8
- MUTE 1–8
- SELECT 1–8

Mapped sequentially per MCU spec.

### 5.3 Faders in MCU Mode
- Sent as Pitch Bend messages (14-bit) per MCU channel.
- Channels 1–8 for faders 1–8, channel 9 for master.

### 5.4 Encoders (V-Pots) in MCU Mode
- Relative value encoding (signed delta).
- CC numbers follow standard MCU V-Pot mapping.

---

## 6. JOG WHEEL (DAW PORT)

- Acts as MCU Scrub Wheel.
- Sends CC 60 with relative delta values.
- Positive values = clockwise; negative values encoded as MCU “signed” deltas.

---

## 7. LED CONTROL (SysEx via DAW Port)

### 7.1 SysEx Envelope
`F0 00 20 6B 7F 42 <payload> F7`

### 7.2 Monochrome LEDs
`F0 00 20 6B 7F 42 02 00 10 <LED_ID> <BRIGHTNESS> F7`

- BRIGHTNESS: 0x00–0x7F

### 7.3 RGB LEDs
`F0 00 20 6B 7F 42 02 00 16 <LED_ID> <R> <G> <B> F7`

- R, G, B: 0x00–0x1F

---

## 8. LED ID TABLES

### 8.1 Top Panel / Navigation
| LED_ID | Control |
|--------|---------|
| 0x10 | OCT− |
| 0x11 | OCT+ |
| 0x12 | CHORD |
| 0x13 | TRANSPOSE |
| 0x14 | MIDI |
| 0x15 | CHORD TRANSPOSE |
| 0x16 | CHORD MEMORY |
| 0x17 | PAD |
| 0x18 | CATEGORY |
| 0x19 | PRESET |
| 0x1C | ANALOG LAB |
| 0x1D | DAW |
| 0x1E | USER |
| 0x1F | PART 1 |
| 0x20 | PART 2 |
| 0x21 | LIVE |
| 0x22–0x29 | SELECT 1–8 |
| 0x2A | MULTI |

### 8.2 DAW Command Center
| LED_ID | Button |
|--------|--------|
| 0x60 | SOLO |
| 0x61 | MUTE |
| 0x62 | RECORD (trk) |
| 0x63 | READ |
| 0x64 | WRITE |
| 0x65 | SAVE |
| 0x66 | IN |
| 0x67 | OUT |
| 0x68 | MARKER |
| 0x69 | UNDO |

### 8.3 Transport
| LED_ID | Button |
|--------|--------|
| 0x6A | REWIND |
| 0x6B | F.FWD |
| 0x6C | STOP |
| 0x6D | PLAY |
| 0x6E | RECORD |
| 0x6F | LOOP |

### 8.4 Pads (RGB)
| LED_ID | Pad |
|--------|-----|
| 0x70 | Pad 1 |
| 0x71 | Pad 2 |
| 0x72 | Pad 3 |
| 0x73 | Pad 4 |
| 0x74 | Pad 5 |
| 0x75 | Pad 6 |
| 0x76 | Pad 7 |
| 0x77 | Pad 8 |

---

## 9. PAD COLOR RANGE (RGB)

Intensity per channel: 0x00–0x1F

| Color | R | G | B |
|-------|---|---|---|
| Off | 00 | 00 | 00 |
| Red | 1F | 00 | 00 |
| Green | 00 | 1F | 00 |
| Blue | 00 | 00 | 1F |
| Yellow | 1F | 1F | 00 |
| Magenta | 1F | 00 | 1F |
| Cyan | 00 | 1F | 1F |
| White | 1F | 1F | 1F |

---

## 10. LCD CONTROL (SysEx via DAW Port)

### 10.1 SysEx Structure
F0 00 20 6B 7F 42 04 00 60 01
<line1 ASCII bytes>
00 02
<line2 ASCII bytes>
00
F7

- Two lines, 16 characters max each.
- ASCII only.

---

## 11. NOTES & CAVEATS

- Arturia does not officially publish full SysEx for Essential series; LED/LCD formats & IDs are derived from MkII family behavior and community reverse-engineering.
- MCU/HUI note/CC layout follows standard Mackie Control specification.
- Firmware variations may slightly change LED IDs or jog wheel encoding.
