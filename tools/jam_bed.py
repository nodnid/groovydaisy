#!/usr/bin/env python3
"""Set a jam bed and leave it looping — then hand the stage to the human.

Lays down the Lullaby's bones as living loops (A minor, 80 BPM, swung):
  loop 1  Warm Pad chords, 4 bars, cutoff sweep RECORDED into the loop
  loop 2  low line, 2 bars (re-voiced by whatever patch is loaded)
  loop 3  the midnight kit, 2 bars (kick down+long, hats tight)

Then: guitar strip gets the same space (reverb + a whisper of delay),
scene 1 = full band / scene 2 = drums out are pre-saved for section
switching from the app, and the transport is LEFT PLAYING.

Capture your guitar: DOUBLE-press the KeyLab Live button (single press
grabs pads+keys). Undo = Pod button 2. Swing/FX live on Bank Live.
"""
import os, sys, termios, time

PORT_CANDIDATES = ["/dev/cu.usbmodem5", "/dev/cu.usbmodem3973397D33331"]

CMD_PLAY, CMD_STOP, CMD_REWIND, CMD_TEMPO = 0x80, 0x81, 0x82, 0x83
CMD_LOAD_PRESET, CMD_SET_BANK, CMD_MIXER, CMD_METRO = 0x86, 0x87, 0x88, 0x89
CMD_MIDI_INJECT = 0x8B
CMD_CAPTURE, CMD_GROOVE, CMD_FX, CMD_SCENE = 0xA0, 0xA6, 0xA7, 0xA9
MSG_CAPTURE = 0x12
SRC_PADS, SRC_KEYS = 0, 1
STRIP_GUITAR, STRIP_SYNTH, STRIP_DRUMS = 32, 33, 34
PRESET_PAD = 1

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
            print(f"• setting the stage from {p}")
            return fd
        except OSError:
            continue
    sys.exit("no serial port")


fd = open_port()
send = lambda b: os.write(fd, b)
_raw = b""
captured = []


def pump(dur=0.0):
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
                captured.append((body[3], body[4]))
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
    cc(num, 64)
    time.sleep(0.02)
    cc(num, val)


# --- soundcheck --------------------------------------------------------------
send(frame(CMD_STOP)); pump(0.1)
send(frame(CMD_REWIND)); pump(0.1)
send(frame(CMD_METRO, bytes([0, 40])))
send(frame(CMD_TEMPO, bytes([800 & 0xFF, 800 >> 8])))

send(frame(CMD_FX, bytes([0, 104])))  # reverb: generous
send(frame(CMD_FX, bytes([1, 78])))   # warm
send(frame(CMD_FX, bytes([2, 1])))    # dotted 8th
send(frame(CMD_FX, bytes([3, 58])))   # gentle repeats
send(frame(CMD_MIXER, bytes([STRIP_SYNTH, 3, 40])))   # synth -> reverb
send(frame(CMD_MIXER, bytes([STRIP_SYNTH, 4, 26])))   # + a little delay
send(frame(CMD_MIXER, bytes([STRIP_DRUMS, 3, 26])))
# the guitar joins the same room: reverb + a whisper of delay
send(frame(CMD_MIXER, bytes([STRIP_GUITAR, 3, 44])))
send(frame(CMD_MIXER, bytes([STRIP_GUITAR, 4, 22])))

send(frame(CMD_GROOVE, bytes([2, 58])))  # swing 58%
send(frame(CMD_GROOVE, bytes([0, 1])))   # pads light quantize
send(frame(CMD_GROOVE, bytes([1, 1])))   # keys light quantize

send(frame(CMD_SET_BANK, bytes([3])))    # midnight kit tuning
time.sleep(0.05)
cc(74, 52)
fader(73, 104)
fader(79, 26)
cc(16, 56)
time.sleep(0.05)
send(frame(CMD_SET_BANK, bytes([2])))
time.sleep(0.05)
send(frame(CMD_LOAD_PRESET, bytes([PRESET_PAD])))

print("• count-in…")
send(frame(CMD_PLAY))
grid = time.monotonic() + BAR + 0.01


def at(bar, s=0.0):
    return grid + bar * BAR + s * S16


events = []


def sched(t, kind, *args):
    events.append((t, kind, args))


# bars 0-3: pad chords + recorded sweep -> 4-bar loop
A2, E3, A3, C4 = 45, 52, 57, 60
F2, C3 = 41, 48
for base, ns in ((0, [(A2, 0, 14), (E3, 1, 13), (A3, 4, 10), (C4, 6, 8)]),
                 (2, [(F2, 0, 14), (C3, 1, 13), (A3, 4, 10), (C4, 6, 8)])):
    for n, start, dur in ns:
        sched(at(base, start), "on", 0, n, 60)
        sched(at(base, start + dur * 2), "off", 0, n)
for k in range(24):
    sched(at(0, k * (64 / 24.0)), "cc", 74, 44 + int(k * 2.2))
sched(at(4, 1.5), "capture", SRC_KEYS, 4)
sched(at(0, 0), "say", "bars 1-4: pad + sweep going down as loop 1")

# bars 4-5: low line -> 2-bar loop (same patch, lives an octave down)
sched(at(4, 0), "say", "bars 5-6: the low line, loop 2")
for b, s, n, dur in ((4, 0, 33, 10), (4, 12, 40, 3), (5, 0, 33, 6),
                     (5, 8, 36, 4), (5, 12, 40, 3)):
    sched(at(b, s), "on", 0, n, 74)
    sched(at(b, s + dur), "off", 0, n)
sched(at(6, 1.5), "capture", SRC_KEYS, 2)

# bars 6-7: the kit -> 2-bar loop
K, HC, HO, RIM = 36, 38, 39, 43
sched(at(6, 0), "say", "bars 7-8: the midnight kit, loop 3")
for b in (6, 7):
    sched(at(b, 0), "on", 9, K, 96)
    sched(at(b, 7), "on", 9, K, 70)
    sched(at(b, 8), "on", 9, K, 88)
    sched(at(b, 4), "on", 9, RIM, 58)
    sched(at(b, 12), "on", 9, RIM, 64)
    for s in range(0, 16, 2):
        sched(at(b, s), "on", 9, HC, 46 if s % 4 else 60)
sched(at(7, 14), "on", 9, HO, 50)
sched(at(8, 1.5), "capture", SRC_PADS, 2)

sched(at(8, 8), "scenes", None)
sched(at(9, 0), "say", "the bed is looping — it's all yours")

events.sort(key=lambda e: e[0])

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
    elif kind == "say":
        print(f"• {args[0]}")
    elif kind == "scenes":
        pump(0.2)
        if len(captured) >= 3:
            drum_slot = captured[2][0]
            send(frame(CMD_SCENE, bytes([0, 0])))            # scene 1: all
            send(frame(CMD_MIXER, bytes([drum_slot, 2, 1])))
            pump(0.05)
            send(frame(CMD_SCENE, bytes([0, 1])))            # scene 2: no drums
            send(frame(CMD_MIXER, bytes([drum_slot, 2, 0])))
            pump(0.05)
            send(frame(CMD_SCENE, bytes([1, 0])))

pump(0.5)
os.close(fd)
print("• loops live, guitar strip in the space, scenes 1/2 saved.")
print("• DOUBLE-press Live to capture guitar · single press grabs pads+keys")
print("• Pod button 2 = undo · Bank Live encoders = swing/reverb/delay")
print("• jam well 🎸")
