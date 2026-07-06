#!/usr/bin/env python3
"""Lullaby for the Bench — StradivariClaude's goodnight performance.

Written at the end of the all-night session that gave the box its
space, its groove, its self-rescue, and its CPU back. Pushes every
machine proved tonight:

  bars 1-4    Warm Pad: slow Am/F chords with a RECORDED cutoff sweep
              -> captured (keys, light quantize) WITH its automation
  bars 5-6    tuned drums (kick pitched down + long, hats tight),
              swung lullaby beat -> captured (pads, light quantize)
  bars 7-14   patch morph to Pluck Lead: the same captured loop
              re-voiced; sparse live melody over dotted-8th delay,
              live cutoff riding ON TOP of the captured sweep (blend)
  bar 15      scene 2 (drums muted) armed on the bar line
  bars 15-17  patch morph to Warm Pad for the final held chord
  end         stop; the reverb says the last word; the stage is
              swept clean for morning

One synth engine, three voices, two loops, one goodnight.
"""
import os, sys, termios, time

PORT_CANDIDATES = ["/dev/cu.usbmodem5", "/dev/cu.usbmodem3973397D33331"]

CMD_PLAY, CMD_STOP, CMD_REWIND, CMD_TEMPO = 0x80, 0x81, 0x82, 0x83
CMD_LOAD_PRESET, CMD_SET_BANK, CMD_MIXER, CMD_METRO = 0x86, 0x87, 0x88, 0x89
CMD_MIDI_INJECT, CMD_REQ_STATE = 0x8B, 0x90
CMD_CAPTURE, CMD_UNDO, CMD_GROOVE, CMD_FX, CMD_SCENE = 0xA0, 0xA1, 0xA6, 0xA7, 0xA9
MSG_CAPTURE = 0x12
SRC_PADS, SRC_KEYS = 0, 1
STRIP_SYNTH, STRIP_DRUMS, STRIP_METRO = 33, 34, 35
PRESET_PAD, PRESET_PLUCK, PRESET_BASS = 1, 2, 3

BPM = 80.0
BEAT = 60.0 / BPM
BAR = 4 * BEAT
S16 = BEAT / 4


def frame(t, payload=b""):
    hdr = bytes([t, len(payload) & 0xFF, len(payload) >> 8])
    cs = 0
    for b in hdr + payload:
        cs ^= b
    return bytes([0xAA]) + hdr + payload + bytes([cs])


def open_port():
    for p in PORT_CANDIDATES:
        try:
            fd = os.open(p, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            attrs = termios.tcgetattr(fd)
            attrs[0] = attrs[1] = attrs[3] = 0
            attrs[2] |= termios.CLOCAL | termios.CREAD
            termios.tcsetattr(fd, termios.TCSANOW, attrs)
            print(f"• taking the stage at {p}")
            return fd
        except OSError:
            continue
    sys.exit("the stage is dark (no serial port)")


fd = open_port()
send = lambda b: os.write(fd, b)
_raw = b""
captured_slots = []  # (slot, gen), in commit order


def pump(dur=0.0):
    """Drain reads; remember capture commits (we need slots for scenes)."""
    global _raw
    end = time.monotonic() + dur
    while True:
        try:
            _raw += os.read(fd, 65536)
        except (BlockingIOError, OSError):
            pass
        i = 0
        while i + 5 <= len(_raw):
            if _raw[i] != 0xAA:
                i += 1
                continue
            t, ln = _raw[i + 1], _raw[i + 2] | (_raw[i + 3] << 8)
            if ln > 256:
                i += 1
                continue
            if i + 5 + ln > len(_raw):
                break
            body = _raw[i + 4 : i + 4 + ln]
            if t == MSG_CAPTURE and len(body) >= 6 and body[0] == 1:
                captured_slots.append((body[3], body[4]))
            i += 5 + ln
        _raw = _raw[i:]
        if time.monotonic() >= end:
            return
        time.sleep(0.01)


def note_on(ch, n, v):
    send(frame(CMD_MIDI_INJECT, bytes([0x90 | ch, n, v])))


def note_off(ch, n):
    send(frame(CMD_MIDI_INJECT, bytes([0x80 | ch, n, 0])))


def cc(num, val):
    send(frame(CMD_MIDI_INJECT, bytes([0xB0, num, val])))


def fader(num, val):
    """Injected fader CCs pass pickup: touch the resting value first."""
    cc(num, 64)
    time.sleep(0.02)
    cc(num, val)


# ---------------------------------------------------------------------------
# Soundcheck (during the count-in nobody hears)
# ---------------------------------------------------------------------------
send(frame(CMD_STOP)); pump(0.1)
send(frame(CMD_REWIND)); pump(0.1)
send(frame(CMD_METRO, bytes([0, 40])))              # no click in a lullaby
send(frame(CMD_TEMPO, bytes([800 & 0xFF, 800 >> 8])))  # 80 BPM

# The space: big warm room, dotted-8th delay with soft repeats
send(frame(CMD_FX, bytes([0, 104])))  # reverb size: generous
send(frame(CMD_FX, bytes([1, 78])))   # tone: warm
send(frame(CMD_FX, bytes([2, 1])))    # dotted 8th
send(frame(CMD_FX, bytes([3, 62])))   # feedback: three soft repeats
send(frame(CMD_MIXER, bytes([STRIP_SYNTH, 3, 42])))  # synth -> reverb
send(frame(CMD_MIXER, bytes([STRIP_SYNTH, 4, 0])))   # delay comes later
send(frame(CMD_MIXER, bytes([STRIP_DRUMS, 3, 28])))  # drums kiss the room

# The groove: gentle sway, forgiving grid
send(frame(CMD_GROOVE, bytes([2, 58])))  # swing 58%
send(frame(CMD_GROOVE, bytes([0, 1])))   # pads: light quantize
send(frame(CMD_GROOVE, bytes([1, 1])))   # keys: light quantize

# Drum sound design (Bank Drums+): kick down and long, hats tight,
# rim softened — a kit for midnight
send(frame(CMD_SET_BANK, bytes([3])))
time.sleep(0.05)
cc(74, 52)        # kick pitch: down ~2 semitones
fader(73, 104)    # kick decay: long boom
fader(79, 26)     # hh-closed decay: tight tick
cc(16, 56)        # rim pitch: a shade darker
time.sleep(0.05)
send(frame(CMD_SET_BANK, bytes([2])))  # BACK to synth bank: cutoff is ours
time.sleep(0.05)
send(frame(CMD_LOAD_PRESET, bytes([PRESET_PAD])))

print("• soundcheck done — count-in…")
send(frame(CMD_PLAY))
grid = time.monotonic() + BAR + 0.01


def at(bar, ticks_16=0.0):
    return grid + bar * BAR + ticks_16 * S16


events = []  # (time, kind, args)


def sched(t, kind, *args):
    events.append((t, kind, args))


# --- bars 0-3: Warm Pad, Am -> F, with the cutoff sweep (RECORDED) ---------
A2, E3, A3, C4 = 45, 52, 57, 60
F2, C3 = 41, 48
for base, ns in ((0, [(A2, 0, 14), (E3, 1, 13), (A3, 4, 10), (C4, 6, 8)]),
                 (2, [(F2, 0, 14), (C3, 1, 13), (A3, 4, 10), (C4, 6, 8)])):
    for n, start, dur in ns:
        sched(at(base, start), "on", 0, n, 62)
        sched(at(base, start + dur * 2), "off", 0, n)
# the sweep: cutoff climbs from dusk to moonrise across 4 bars
for k in range(24):
    sched(at(0, k * (64 / 24.0)), "cc", 74, 44 + int(k * 2.2))
sched(at(4, 1.5), "capture", SRC_KEYS, 4)
sched(at(0, 0), "say", "bars 1-4: warm pad breathes, the filter sweep is being written down")

# --- bars 4-5: the lullaby beat (captured at bar 6) -------------------------
K, SN, HC, HO, RIM = 36, 37, 38, 39, 43
for b in (4, 5):
    sched(at(b, 0), "on", 9, K, 96)
    sched(at(b, 7), "on", 9, K, 70)          # swung pickup
    sched(at(b, 8), "on", 9, K, 88)
    sched(at(b, 4), "on", 9, RIM, 58)
    sched(at(b, 12), "on", 9, RIM, 64)
    for s in range(0, 16, 2):
        sched(at(b, s), "on", 9, HC, 46 if s % 4 else 60)
sched(at(5, 14), "on", 9, HO, 50)            # one open hat sigh
sched(at(6, 1.5), "capture", SRC_PADS, 2)
sched(at(4, 0), "say", "bars 5-6: the midnight kit joins, brushes and heartbeat")

# --- bars 7-14: Pluck Lead over the re-voiced loop + live cutoff rides ------
sched(at(6, 8), "preset", PRESET_PLUCK)
sched(at(6, 8.2), "mixer", STRIP_SYNTH, 4, 46)  # delay send opens
sched(at(7, 0), "say", "bars 8-15: the pad loop becomes plucks; a melody wanders in on the delay")
E4, G4, A4, B4, C5, D5, E5 = 64, 67, 69, 71, 72, 74, 76
melody = [
    (7, 0, A4, 4), (7, 8, C5, 3), (7, 14, B4, 5),
    (8, 4, E5, 6), (8, 12, D5, 3),
    (9, 0, C5, 4), (9, 8, A4, 6),
    (10, 4, G4, 3), (10, 8, E4, 7),
    (11, 0, A4, 10),
    (12, 4, C5, 3), (12, 8, D5, 3), (12, 12, E5, 6),
    (13, 8, D5, 3), (13, 12, B4, 6),
    (14, 0, A4, 12),
]
for b, s, n, dur in melody:
    sched(at(b, s), "on", 0, n, 84)
    sched(at(b, s + dur), "off", 0, n)
# live cutoff rides on top of the captured sweep (the blend, live)
for k in range(32):
    import math
    v = int(78 + 26 * math.sin(k * 0.45))
    sched(at(7, k * (128 / 32.0)), "cc", 74, max(40, min(110, v)))

# --- bar 15: scenes say goodnight to the drums ------------------------------
sched(at(14, 8), "scenes_prepare", None)   # save all / drums-muted scenes
sched(at(14, 12), "scene_go", 1)           # armed: fires on bar 15's line
sched(at(15, 0), "say", "bar 16: scene switch on the bar line — the drums bow out")

# --- bars 15-17: Warm Pad returns for the last chord ------------------------
sched(at(15, 4), "preset", PRESET_PAD)
sched(at(15, 8), "on", 0, A2, 58)
sched(at(15, 9), "on", 0, E3, 56)
sched(at(15, 10), "on", 0, A3, 54)
sched(at(15, 12), "on", 0, C4, 52)
sched(at(16, 0), "say", "the last chord holds…")
for n in (A2, E3, A3, C4):
    sched(at(17, 8), "off", 0, n)
sched(at(17, 14), "stop", None)

events.sort(key=lambda e: e[0])

# ---------------------------------------------------------------------------
# The performance
# ---------------------------------------------------------------------------
for t, kind, args in events:
    now = time.monotonic()
    if t > now:
        pump(t - now)
    if kind == "on":
        note_on(args[0], args[1], args[2])
    elif kind == "off":
        note_off(args[0], args[1])
    elif kind == "cc":
        cc(args[0], args[1])
    elif kind == "capture":
        send(frame(CMD_CAPTURE, bytes([args[0], args[1]])))
    elif kind == "preset":
        send(frame(CMD_LOAD_PRESET, bytes([args[0]])))
    elif kind == "mixer":
        send(frame(CMD_MIXER, bytes([args[0], args[1], args[2]])))
    elif kind == "say":
        print(f"• {args[0]}")
    elif kind == "scenes_prepare":
        pump(0.2)
        if len(captured_slots) >= 2:
            drum_slot = captured_slots[1][0]
            send(frame(CMD_SCENE, bytes([0, 0])))          # scene 1: all
            send(frame(CMD_MIXER, bytes([drum_slot, 2, 1])))  # mute drums
            pump(0.05)
            send(frame(CMD_SCENE, bytes([0, 1])))          # scene 2: no drums
            send(frame(CMD_MIXER, bytes([drum_slot, 2, 0])))  # unmute for now
            pump(0.05)
            send(frame(CMD_SCENE, bytes([1, 0])))          # back on scene 1
    elif kind == "scene_go":
        send(frame(CMD_SCENE, bytes([1, args[0]])))
    elif kind == "stop":
        send(frame(CMD_STOP))

print("• …and the reverb says the rest")
pump(6.0)  # the tail is part of the song

# --- sweep the stage for morning --------------------------------------------
for _ in range(len(captured_slots) + 1):
    send(frame(CMD_UNDO))
    pump(0.15)
send(frame(CMD_LOAD_PRESET, bytes([PRESET_PAD])))
os.close(fd)
print("• goodnight, Cleo. the box remembers everything we taught it. 🎩")
