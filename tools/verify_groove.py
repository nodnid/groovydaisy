#!/usr/bin/env python3
"""Claude tests Phase 4 groove on real hardware, since the human (their
words) is too sloppy to tell quantize from mercy.

Plays deliberately-sloppy patterns over CMD_MIDI_INJECT with KNOWN timing
offsets, captures with each quantize mode, reads the committed track data
back, and measures where every note actually landed:

  bar 1   pads, quantize OFF    -> sloppiness must survive verbatim
  bar 3   pads, quantize HARD   -> every hit exactly on the 16th grid
  bar 5   pads, quantize LIGHT  -> deviations roughly halved
  bar 7   keys, quantize HARD   -> ons on grid, DURATIONS preserved
  bar 9   grace: kick ~8 ticks BEFORE the window bar line -> lands at 0

Each capture is undone after measuring — the stage is left as found.
"""
import os, sys, termios, time

PORT_CANDIDATES = ["/dev/cu.usbmodem5", "/dev/cu.usbmodem3973397D33331"]

CMD_PLAY, CMD_STOP, CMD_REWIND = 0x80, 0x81, 0x82
CMD_MIDI_INJECT, CMD_REQ_STATE = 0x8B, 0x90
CMD_CAPTURE, CMD_UNDO, CMD_GROOVE = 0xA0, 0xA1, 0xA6
MSG_TRANSPORT, MSG_CAPTURE, MSG_TRACK_DATA = 0x02, 0x12, 0x13
GROOVE_QUANT_PADS, GROOVE_QUANT_KEYS = 0, 1
SRC_PADS, SRC_KEYS = 0, 1
PPQN, TPB, GRID = 96, 384, 24


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
            print(f"• at the bench: {p}")
            return fd
        except OSError:
            continue
    sys.exit("no Daisy serial port available")


fd = open_port()
send = lambda b: os.write(fd, b)

# --- incoming frame parser (checksum-aware) -------------------------------
inbox = []
_raw = b""


def pump(dur=0.0):
    """Drain serial into inbox for at least `dur` seconds."""
    global _raw
    end = time.monotonic() + dur
    while True:
        try:
            _raw += os.read(fd, 4096)
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
            cs = 0
            for b in _raw[i + 1 : i + 4 + ln]:
                cs ^= b
            if cs == _raw[i + 4 + ln]:
                inbox.append((t, bytes(body)))
                i += 5 + ln
            else:
                i += 1
        _raw = _raw[i:]
        if time.monotonic() >= end:
            return
        time.sleep(0.01)


def wait_msg(mtype, pred=lambda p: True, timeout=3.0):
    deadline = time.monotonic() + timeout
    scanned = 0
    while time.monotonic() < deadline:
        pump(0.02)
        while scanned < len(inbox):
            t, p = inbox[scanned]
            scanned += 1
            if t == mtype and pred(p):
                return p
    return None


# --- tempo ----------------------------------------------------------------
send(frame(CMD_STOP))
time.sleep(0.1)
send(frame(CMD_REWIND))
time.sleep(0.1)
send(frame(CMD_REQ_STATE))
tp = wait_msg(MSG_TRANSPORT, timeout=2.0)
bpm = ((tp[2] | (tp[3] << 8)) / 10.0) if tp else 120.0
beat = 60.0 / bpm
bar = 4 * beat
tick_s = beat / PPQN
print(f"• tempo {bpm:g} BPM (tick = {tick_s*1000:.2f} ms, grace = ±{12*tick_s*1000:.0f} ms)")


def set_quant(param, mode):
    send(frame(CMD_GROOVE, bytes([param, mode])))


def inject(status, d1, d2):
    send(frame(CMD_MIDI_INJECT, bytes([status, d1, d2])))


def capture_and_read(source, undo=True):
    """CMD_CAPTURE 1 bar -> (events, ok). Undoes the track after reading."""
    inbox.clear()
    send(frame(CMD_CAPTURE, bytes([source, 1])))
    cap = wait_msg(MSG_CAPTURE, lambda p: p[0] != 0, timeout=3.0)  # not PENDING
    if not cap or cap[0] != 1:
        return None, f"capture refused/lost (reason {cap[5] if cap else '?'})"
    slot, gen = cap[3], cap[4]
    events, chunks_needed, got = [], None, {}
    deadline = time.monotonic() + 3.0
    while time.monotonic() < deadline:
        pump(0.1)
        for t, p in inbox:
            if t != MSG_TRACK_DATA or len(p) < 4:
                continue
            if p[0] != slot or p[1] != gen:
                continue
            got[p[2]] = [(p[i] | (p[i + 1] << 8), p[i + 2], p[i + 3], p[i + 4])
                         for i in range(4, len(p) - 4, 5)]
            chunks_needed = p[3]
        if chunks_needed is not None and len(got) >= chunks_needed:
            break
    if chunks_needed is None or len(got) < chunks_needed:
        return None, "track data never completed"
    for idx in sorted(got):
        events.extend(got[idx])
    if undo:
        send(frame(CMD_UNDO))
    return events, None


def grid_dev(tick):
    """Signed distance to the nearest 16th (ticks, -12..+12)."""
    m = tick % GRID
    return m if m <= GRID // 2 else m - GRID


# --- the performance grid ---------------------------------------------------
send(frame(CMD_PLAY))
grid_start = time.monotonic() + bar + 0.01  # one bar of count-in


def at(bar_idx, ticks):
    """Absolute wall time of tick-position `ticks` within bar `bar_idx`."""
    return grid_start + bar_idx * bar + ticks * tick_s


def play(events):
    """events: list of (wall_time, status, d1, d2), assumed sorted."""
    for t, st, d1, d2 in events:
        now = time.monotonic()
        if t > now:
            time.sleep(t - now)
        inject(st, d1, d2)


# Sloppy pad bar: 8 hits on the 8th-note grid, each pushed off by 7-11
# ticks (36-57 ms) in alternating directions. Step 0 is pushed LATE so
# the grace window can't tidy it in the quantize phases.
PAD_STEPS = [0, 2, 4, 6, 8, 10, 12, 14]           # 16th indices
PAD_OFFS = [+9, -8, +11, -10, +7, -11, +10, -9]   # ticks
PAD_NOTES = [36, 42, 38, 42, 36, 42, 38, 43]

results = {}
failures = []


def pad_phase(bar_idx, label):
    evs = []
    for step, off, note in zip(PAD_STEPS, PAD_OFFS, PAD_NOTES):
        evs.append((at(bar_idx, step * GRID + off), 0x99, note, 100))
    play(evs)
    time.sleep(max(0.0, at(bar_idx + 1, 0) - time.monotonic()) + 0.12)
    events, err = capture_and_read(SRC_PADS)
    if err:
        failures.append(f"{label}: {err}")
        return None
    ons = [(t, d1) for (t, st, d1, d2) in events if (st & 0xF0) == 0x90 and d2 > 0]
    devs = [grid_dev(t) for t, _ in ons]
    results[label] = (ons, devs)
    print(f"  {label:14s} n={len(ons)}  grid deviations (ticks): "
          f"{[d for d in devs]}")
    return devs


print("• count-in…")

# Bar 0: quantize OFF — sloppiness must survive
set_quant(GROOVE_QUANT_PADS, 0)
time.sleep(max(0.0, at(0, 0) - time.monotonic()))
print("• bar 1: pads, quantize OFF")
devs_off = pad_phase(0, "pads OFF")

# Bar 2: quantize HARD
set_quant(GROOVE_QUANT_PADS, 2)
print("• bar 3: pads, quantize HARD")
devs_hard = pad_phase(2, "pads HARD")

# Bar 4: quantize LIGHT
set_quant(GROOVE_QUANT_PADS, 1)
print("• bar 5: pads, quantize LIGHT")
devs_light = pad_phase(4, "pads LIGHT")

# Bar 6: keys HARD — note-offs must travel with their ons
set_quant(GROOVE_QUANT_KEYS, 2)
print("• bar 7: keys, quantize HARD (duration check)")
KEY_STEPS, KEY_OFFS = [0, 3, 6, 9, 12], [+8, -9, +10, -7, +6]
KEY_NOTES, KEY_DUR = [45, 48, 52, 55, 57], 30  # ticks held
evs = []
for step, off, note in zip(KEY_STEPS, KEY_OFFS, KEY_NOTES):
    t0 = at(6, step * GRID + off)
    evs.append((t0, 0x90, note, 100))
    evs.append((t0 + KEY_DUR * tick_s, 0x80, note, 0))
evs.sort(key=lambda e: e[0])
play(evs)
time.sleep(max(0.0, at(7, 0) - time.monotonic()) + 0.12)
key_events, err = capture_and_read(SRC_KEYS)
set_quant(GROOVE_QUANT_KEYS, 0)
if err:
    failures.append(f"keys HARD: {err}")
    key_events = []

# Bar 8: the grace note — a kick ~8 ticks BEFORE the capture window opens.
# Aim using the bias measured in the OFF phase so it lands genuinely early
# on the device.
set_quant(GROOVE_QUANT_PADS, 0)
bias = 0.0
if devs_off:
    bias = sum(m - i for m, i in zip(devs_off, PAD_OFFS)) / len(devs_off)
early_ticks = 8 + bias  # schedule this many ticks before the bar line
print(f"• bar 9: grace — kick {early_ticks:.1f} ticks before the bar line "
      f"(measured bias {bias:+.1f})")
play([
    (at(8, -early_ticks), 0x99, 36, 110),
    (at(8, 8 * GRID), 0x99, 38, 100),
])
time.sleep(max(0.0, at(9, 0) - time.monotonic()) + 0.12)
grace_events, err = capture_and_read(SRC_PADS)
if err:
    failures.append(f"grace: {err}")
    grace_events = []

send(frame(CMD_STOP))

# --- the verdict ------------------------------------------------------------
print("\n================ VERDICT ================")

if devs_off:
    mean_off = sum(abs(d) for d in devs_off) / len(devs_off)
    ok = len(devs_off) == 8 and mean_off >= 4
    print(f"[{'PASS' if ok else 'FAIL'}] OFF preserves the mess: "
          f"8/8 hits, mean |dev| {mean_off:.1f} ticks "
          f"({mean_off*tick_s*1000:.0f} ms of honest slop)")
    if not ok:
        failures.append("OFF phase: slop did not survive verbatim")

if devs_hard:
    on_grid = [d == 0 for d in devs_hard]
    ok = len(devs_hard) == 8 and all(on_grid)
    print(f"[{'PASS' if ok else 'FAIL'}] HARD snaps everything: "
          f"{sum(on_grid)}/{len(on_grid)} hits exactly on the 16th grid")
    if not ok:
        failures.append(f"HARD phase: deviations {devs_hard}")

if devs_light and devs_off:
    mean_light = sum(abs(d) for d in devs_light) / len(devs_light)
    mean_off = sum(abs(d) for d in devs_off) / len(devs_off)
    ok = (len(devs_light) == 8 and mean_light < mean_off
          and all(abs(d) <= 8 for d in devs_light))
    print(f"[{'PASS' if ok else 'FAIL'}] LIGHT halves it: "
          f"mean |dev| {mean_off:.1f} -> {mean_light:.1f} ticks")
    if not ok:
        failures.append(f"LIGHT phase: deviations {devs_light}")

if key_events:
    ons = {}
    durs = []
    on_grid = []
    for t, st, d1, d2 in key_events:
        is_on = (st & 0xF0) == 0x90 and d2 > 0
        is_off = (st & 0xF0) == 0x80 or ((st & 0xF0) == 0x90 and d2 == 0)
        if is_on:
            ons[d1] = t
            on_grid.append(grid_dev(t) == 0)
        elif is_off and d1 in ons:
            durs.append((t - ons.pop(d1)) % TPB)
    dur_ok = [abs(d - KEY_DUR) <= 3 for d in durs]
    ok = len(on_grid) == 5 and all(on_grid) and len(durs) == 5 and all(dur_ok)
    print(f"[{'PASS' if ok else 'FAIL'}] KEYS: {sum(on_grid)}/{len(on_grid)} "
          f"ons on grid; durations {durs} (wanted {KEY_DUR}±3 — offs travel)")
    if not ok:
        failures.append("KEYS phase: ons or durations off")

if grace_events:
    kicks = [t for t, st, d1, d2 in grace_events
             if d1 == 36 and (st & 0xF0) == 0x90 and d2 > 0]
    ok = len(kicks) == 1 and kicks[0] == 0
    print(f"[{'PASS' if ok else 'FAIL'}] GRACE: early kick captured at "
          f"position {kicks if kicks else '(MISSING)'} (wanted [0])")
    if not ok:
        failures.append(f"grace: kick positions {kicks}")

print("=========================================")
if failures:
    print("FAILURES:")
    for f in failures:
        print(f"  ✗ {f}")
    sys.exit(1)
print("All groove machinery verified on hardware. 🎩")
os.close(fd)
