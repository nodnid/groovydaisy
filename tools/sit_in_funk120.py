#!/usr/bin/env python3
"""StradivariClaude sits in for a song.

Opens the Daisy's free serial port, reads the current tempo, rewinds,
presses play (count-in!), then performs live over CMD_MIDI_INJECT:
  bars 1-4   drum groove          -> captured as a loop (pads, 4 bars)
  bars 5-8   bassline over it     -> captured as a loop (keys, 4 bars)
  bars 9-12  a little lead solo   (live only, not captured)
  bar 13     held final note, bow.
The loops keep playing after the script exits.
"""
import os, sys, termios, time

PORT_CANDIDATES = ["/dev/cu.usbmodem5", "/dev/cu.usbmodem3973397D33331"]

CMD_PLAY, CMD_REWIND, CMD_TEMPO = 0x80, 0x82, 0x83
CMD_MIDI_INJECT, CMD_REQ_STATE, CMD_CAPTURE = 0x8B, 0x90, 0xA0
MSG_TRANSPORT = 0x02
SRC_PADS, SRC_KEYS = 0, 1

def frame(t, payload=b""):
    hdr = bytes([t, len(payload) & 0xFF, len(payload) >> 8])
    cs = 0
    for b in hdr + payload: cs ^= b
    return bytes([0xAA]) + hdr + payload + bytes([cs])

def open_port():
    for p in PORT_CANDIDATES:
        try:
            fd = os.open(p, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            attrs = termios.tcgetattr(fd)
            attrs[0] = attrs[1] = attrs[3] = 0        # raw
            attrs[2] |= termios.CLOCAL | termios.CREAD
            termios.tcsetattr(fd, termios.TCSANOW, attrs)
            print(f"• sitting down at {p}")
            return fd
        except OSError:
            continue
    sys.exit("no Daisy serial port available")

fd = open_port()
send = lambda b: os.write(fd, b)

# --- what's the band's tempo? ---
send(frame(CMD_REQ_STATE))
bpm, state, buf = None, 0, {}
deadline = time.monotonic() + 1.5
raw = b""
while time.monotonic() < deadline and bpm is None:
    try: raw += os.read(fd, 4096)
    except BlockingIOError: time.sleep(0.02); continue
    i = 0
    while i + 5 <= len(raw):
        if raw[i] != 0xAA: i += 1; continue
        t, ln = raw[i+1], raw[i+2] | (raw[i+3] << 8)
        if i + 5 + ln > len(raw): break
        if t == MSG_TRANSPORT and ln >= 4:
            bpm = (raw[i+4+2] | (raw[i+4+3] << 8)) / 10.0
        i += 5 + ln
    raw = raw[i:]
if bpm is None:
    bpm = 120.0
    send(frame(CMD_TEMPO, bytes([1200 & 0xFF, 1200 >> 8])))  # if unlocked
print(f"• the band plays at {bpm:g} BPM")

beat = 60.0 / bpm
bar = 4 * beat
s16 = beat / 4  # sixteenth

def note_on(ch, n, v):  return frame(CMD_MIDI_INJECT, bytes([0x90 | ch, n, v]))
def note_off(ch, n):    return frame(CMD_MIDI_INJECT, bytes([0x80 | ch, n, 0]))

events = []  # (t_from_grid_start, bytes)
def hit(t, n, v):        events.append((t, note_on(9, n, v)))          # drums
def key(t, n, v, dur):   events.append((t, note_on(0, n, v))); events.append((t + dur, note_off(0, n)))

# --- bars 1-4: the groove (kick 36, snare 38, hats 42/43, toms 45/47) ---
for b in range(4):
    t0 = b * bar
    for step, n, v in [(0, 36, 118), (6, 36, 96), (8, 36, 110), (4, 38, 105), (12, 38, 108)]:
        hit(t0 + step * s16, n, v)
    for step in range(0, 16, 2):
        hit(t0 + step * s16, 42, 72 if step % 4 == 0 else 46)
    if b % 2 == 1:
        hit(t0 + 14 * s16, 43, 80)                                   # open hat
    if b == 3:
        for step, n in [(12, 47), (13, 47), (14, 45), (15, 45)]:      # fill
            hit(t0 + step * s16, n, 100 + step)

# grab the drums just after bar 5's downbeat (nearest-bar rounds back)
events.append((4 * bar + 0.12, frame(CMD_CAPTURE, bytes([SRC_PADS, 4]))))

# --- bars 5-8: bassline (Am / Am / F / G) ---
A2, C3, E3, G3, F2, G2 = 45, 48, 52, 55, 41, 43
lines = [
    [(0, A2, 3), (6, A2, 2), (8, C3, 2), (12, E3, 3)],
    [(0, A2, 3), (6, A2, 2), (8, C3, 2), (12, G3, 3)],
    [(0, F2, 3), (6, F2, 2), (8, A2, 2), (12, C3, 3)],
    [(0, G2, 3), (6, G2, 2), (8, B3 := 59, 2), (12, G3, 3)],
]
for b, line in enumerate(lines):
    t0 = (4 + b) * bar
    for step, n, dur in line:
        key(t0 + step * s16, n, 100, dur * s16 * 0.9)

events.append((8 * bar + 0.12, frame(CMD_CAPTURE, bytes([SRC_KEYS, 4]))))

# --- bars 9-12: a little lead (live only) ---
A4, C5, D5, E5, G5, A5 = 69, 72, 74, 76, 79, 81
phrases = [
    [(0, A4, 2), (4, C5, 2), (8, D5, 2), (12, E5, 4)],
    [(0, E5, 2), (4, D5, 2), (8, C5, 2), (10, D5, 2), (12, A4, 4)],
    [(0, C5, 3), (6, E5, 3), (12, G5, 4)],
    [(0, E5, 2), (4, D5, 2), (8, C5, 2), (12, A4, 2)],
]
for b, ph in enumerate(phrases):
    t0 = (8 + b) * bar
    for step, n, dur in ph:
        key(t0 + step * s16, n, 92, dur * s16 * 0.85)

# --- bar 13: the bow ---
key(12 * bar, A5, 96, 3.2 * beat)

events.sort(key=lambda e: e[0])

# --- performance ---
send(frame(CMD_REWIND)); time.sleep(0.1)
print("• count-in…")
send(frame(CMD_PLAY))
grid_start = time.monotonic() + bar + 0.05  # one bar of count-in

marks = {0: "bars 1-4: drums", 4*bar: "GRAB drums -> loop; bars 5-8: bass",
         8*bar: "GRAB bass -> loop; bars 9-12: lead", 12*bar: "final note"}
for t, data in events:
    for mt in list(marks):
        if t >= mt: print(f"• {marks.pop(mt)}")
    now = time.monotonic()
    if grid_start + t > now: time.sleep(grid_start + t - now)
    send(data)

time.sleep(2)
os.close(fd)
print("• the loops are yours — I'll see myself off the stage. 🎩")
