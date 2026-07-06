#!/usr/bin/env python3
"""Jam #2: half-time swagger in E minor, swung by hand, filter solo."""
import os, sys, termios, time

PORTS = ["/dev/cu.usbmodem5", "/dev/cu.usbmodem3973397D33331"]
CMD_PLAY, CMD_REWIND, CMD_TEMPO = 0x80, 0x82, 0x83
CMD_SET_BANK, CMD_METRO = 0x87, 0x89
CMD_MIDI_INJECT, CMD_REQ_STATE = 0x8B, 0x90
CMD_CAPTURE, CMD_UNDO = 0xA0, 0xA1
MSG_TRANSPORT = 0x02
SRC_PADS, SRC_KEYS = 0, 1

def frame(t, payload=b""):
    hdr = bytes([t, len(payload) & 0xFF, len(payload) >> 8]); cs = 0
    for b in hdr + payload: cs ^= b
    return bytes([0xAA]) + hdr + payload + bytes([cs])

def open_port():
    for p in PORTS:
        try:
            fd = os.open(p, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            a = termios.tcgetattr(fd); a[0] = a[1] = a[3] = 0
            a[2] |= termios.CLOCAL | termios.CREAD
            termios.tcsetattr(fd, termios.TCSANOW, a)
            print(f"• back on stage at {p}"); return fd
        except OSError: continue
    sys.exit("no port")

fd = open_port(); send = lambda b: os.write(fd, b)

def read_bpm(timeout=1.2):
    send(frame(CMD_REQ_STATE)); raw = b""; bpm = None
    end = time.monotonic() + timeout
    while time.monotonic() < end and bpm is None:
        try: raw += os.read(fd, 4096)
        except BlockingIOError: time.sleep(0.02); continue
        i = 0
        while i + 5 <= len(raw):
            if raw[i] != 0xAA: i += 1; continue
            t, ln = raw[i+1], raw[i+2] | (raw[i+3] << 8)
            if i + 5 + ln > len(raw): break
            if t == MSG_TRANSPORT and ln >= 4:
                bpm = (raw[i+6] | (raw[i+7] << 8)) / 10.0
            i += 5 + ln
        raw = raw[i:]
    return bpm

# clear my two loops from the last set (newest-first: bass, then drums)
print("• clearing my loops from the last set (undo x2)")
send(frame(CMD_UNDO)); time.sleep(0.15); send(frame(CMD_UNDO)); time.sleep(0.15)

# ask for 88 BPM; the box may refuse if tempo-locked — play what it says
send(frame(CMD_TEMPO, bytes([880 & 0xFF, 880 >> 8]))); time.sleep(0.2)
bpm = read_bpm() or 120.0
print(f"• tonight we play at {bpm:g} BPM")
beat = 60.0 / bpm; bar = 4 * beat; s16 = beat / 4
SW = 0.36 * s16  # hand-rolled swing on the off-eighths

def on(ch, n, v):  return frame(CMD_MIDI_INJECT, bytes([0x90 | ch, n, v]))
def off(ch, n):    return frame(CMD_MIDI_INJECT, bytes([0x80 | ch, n, 0]))
def cc(num, val):  return frame(CMD_MIDI_INJECT, bytes([0xB0, num, val]))

ev = []
def hit(t, n, v): ev.append((t, on(9, n, v)))
def key(t, n, v, d): ev.append((t, on(0, n, v))); ev.append((t + d, off(0, n)))

def swung(step):  # swing the odd 8ths (steps 2,6,10,14 of the 16th grid)
    return step * s16 + (SW if step % 4 == 2 else 0)

# bars 1-4: half-time — kick on 1, big snare on 3, swung sparse hats
for b in range(4):
    t0 = b * bar
    hit(t0, 36, 120)
    hit(t0 + 8 * s16, 38, 112)                      # half-time backbeat
    if b in (1, 3): hit(t0 + 15 * s16, 36, 78)      # pickup kick
    for step in (0, 2, 4, 6, 8, 10, 12, 14):
        hit(t0 + swung(step), 42, 68 if step % 8 == 0 else 40)
    if b == 3:
        hit(t0 + 12 * s16, 39, 96)                  # rim... clap actually
        hit(t0 + 14 * s16, 45, 104)
ev.append((4 * bar + 0.12, frame(CMD_CAPTURE, bytes([SRC_PADS, 4]))))

# bars 5-8: dark bass, Em (E2 38... E2=40) — Em/Em/C/D
E2, G2, A2, B2, C3, D3 = 40, 43, 45, 47, 48, 50
lines = [
    [(0, E2, 5), (8, E2, 2), (10, G2, 2), (14, A2, 2)],
    [(0, E2, 5), (8, B2, 2), (10, A2, 2), (14, G2, 2)],
    [(0, C3, 5), (8, C3, 2), (10, G2, 2), (14, E2, 2)],
    [(0, D3, 3), (6, D3, 2), (8, A2, 3), (14, B2, 2)],
]
for b, line in enumerate(lines):
    t0 = (4 + b) * bar
    for step, n, d in line:
        key(t0 + swung(step), n, 102, d * s16 * 0.9)
ev.append((8 * bar + 0.12, frame(CMD_CAPTURE, bytes([SRC_KEYS, 4]))))

# bars 9-12: solo with a live filter sweep (make sure CC74 = cutoff)
ev.append((8 * bar + 0.2, frame(CMD_SET_BANK, bytes([2]))))  # Synth bank
E4, G4, A4, B4, D5, E5, G5 = 64, 67, 69, 71, 74, 76, 79
phr = [
    [(0, E4, 3), (6, G4, 3), (12, A4, 4)],
    [(0, B4, 2), (4, A4, 2), (8, G4, 2), (10, A4, 2), (12, E4, 4)],
    [(0, D5, 3), (6, E5, 3), (12, G5, 4)],
    [(0, E5, 2), (4, D5, 2), (8, B4, 2), (12, E4, 6)],
]
for b, p in enumerate(phr):
    t0 = (8 + b) * bar
    for step, n, d in p:
        key(t0 + swung(step), n, 90, d * s16 * 0.85)
# the sweep: close the filter, then open it across the last two solo bars
for i in range(25):
    ev.append((10 * bar + i * (2 * bar / 25), cc(74, 30 + int(i * 97 / 24))))

ev.sort(key=lambda e: e[0])

send(frame(CMD_REWIND)); time.sleep(0.1)
print("• count-in… (then the click sits out)")
send(frame(CMD_PLAY))
grid = time.monotonic() + bar + 0.05
ev.insert(0, (0.01, frame(CMD_METRO, bytes([0, 64]))))  # click off post-count-in

marks = {0: "bars 1-4: half-time drums, hand-swung",
         4*bar: "GRAB drums; bars 5-8: dark bass",
         8*bar: "GRAB bass; bars 9-12: solo + filter sweep",
         12*bar: "let it breathe"}
for t, data in ev:
    for mt in list(marks):
        if t >= mt: print(f"• {marks.pop(mt)}")
    now = time.monotonic()
    if grid + t > now: time.sleep(grid + t - now)
    send(data)

time.sleep(1.5); os.close(fd)
print("• two loops left spinning for you. encore's on the KeyLab. 🎩")
