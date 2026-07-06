#!/usr/bin/env python3
"""The 30-minute soak (Phase 6 giggability): jam on the box continuously
and watch the diagnostics. A campfire session is an hour of uninterrupted
trust; this earns it.

Traffic, all at once, for the whole duration:
  - a drum groove + bass phrases with CC sweeps (constant MIDI + automation)
  - a capture every ~8 bars, cycling create/undo so slots churn
  - lane edits on random live tracks (toggle drum cells)
  - scene saves/switches
  - swing + FX param tweaks
  - a play/stop/play spam burst every ~4 minutes (the panic test)

Failure conditions:
  - MSG_HELLO mid-soak (the box rebooted = it crashed)
  - MSG_STATS reporting ring drops
  - checksum errors on the wire
  - CPU peak >= 95%
  - capture refusals other than the expected kinds

Usage: python3 tools/soak.py [minutes]   (default 30; try 3 first)
"""
import os, sys, termios, time, random

MINUTES = float(sys.argv[1]) if len(sys.argv) > 1 else 30.0
random.seed(0xDA151)  # reproducible torture

PORT_CANDIDATES = ["/dev/cu.usbmodem5", "/dev/cu.usbmodem3973397D33331"]

CMD_PLAY, CMD_STOP, CMD_REWIND = 0x80, 0x81, 0x82
CMD_MIDI_INJECT, CMD_REQ_STATE = 0x8B, 0x90
CMD_CAPTURE, CMD_UNDO, CMD_GROOVE = 0xA0, 0xA1, 0xA6
CMD_FX, CMD_TRACK_EDIT, CMD_SCENE = 0xA7, 0xA8, 0xA9
MSG_HELLO, MSG_TRANSPORT, MSG_ERROR = 0x01, 0x02, 0x0B
MSG_TRACK, MSG_TRACK_GONE, MSG_CAPTURE = 0x10, 0x11, 0x12
MSG_METERS, MSG_STATS = 0x22, 0x23
SRC_PADS, SRC_KEYS = 0, 1
GRID = 24


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

# --- monitors ------------------------------------------------------------------
stats = {
    "hello_reboots": 0,
    "checksum_errors": 0,
    "ring_drop_frames": 0,
    "cpu_max": 0,
    "errors": {},
    "captures_ok": 0,
    "captures_refused": 0,
    "tracks_live": [],  # (slot, gen, kind)
    "sent": 0,
    "edits": 0,
    "scenes": 0,
}
_raw = b""
started = False


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
                stats["checksum_errors"] += 1
                continue
            if i + 5 + ln > len(_raw):
                break
            body = _raw[i + 4 : i + 4 + ln]
            cs = 0
            for b in _raw[i + 1 : i + 4 + ln]:
                cs ^= b
            if cs != _raw[i + 4 + ln]:
                stats["checksum_errors"] += 1
                i += 1
                continue
            handle(t, bytes(body))
            i += 5 + ln
        _raw = _raw[i:]
        if time.monotonic() >= end:
            return
        time.sleep(0.005)


def handle(t, p):
    if t == MSG_HELLO and started:
        stats["hello_reboots"] += 1  # the box came back from the dead
    elif t == MSG_METERS and len(p) >= 3:
        stats["cpu_max"] = max(stats["cpu_max"], p[2])
    elif t == MSG_STATS:
        stats["ring_drop_frames"] += 1
    elif t == MSG_ERROR and len(p) >= 1:
        stats["errors"][p[0]] = stats["errors"].get(p[0], 0) + 1
    elif t == MSG_CAPTURE and len(p) >= 6:
        if p[0] == 1:
            stats["captures_ok"] += 1
            stats["tracks_live"].append((p[3], p[4]))
        elif p[0] == 2:
            stats["captures_refused"] += 1
    elif t == MSG_TRACK and len(p) >= 2:
        # gen bumps (edits) refresh our reference
        stats["tracks_live"] = [
            (s, p[1]) if s == p[0] else (s, g) for s, g in stats["tracks_live"]
        ]
    elif t == MSG_TRACK_GONE and len(p) >= 2:
        stats["tracks_live"] = [
            (s, g) for s, g in stats["tracks_live"] if s != p[0]
        ]


def inject(status, d1, d2):
    send(frame(CMD_MIDI_INJECT, bytes([status, d1, d2])))
    stats["sent"] += 1


# --- setup -----------------------------------------------------------------------
send(frame(CMD_STOP)); pump(0.1)
send(frame(CMD_REWIND)); pump(0.1)
send(frame(CMD_REQ_STATE))
pump(1.0)
started = True
bpm = 120.0  # soak at the default; tempo tweaks are part of the torture
beat = 60.0 / bpm
bar = 4 * beat
s16 = beat / 4

send(frame(CMD_PLAY))
grid_start = time.monotonic() + bar + 0.01
end_time = grid_start + MINUTES * 60
print(f"• soaking for {MINUTES:g} minutes at {bpm:g} BPM…")

DRUM = [(0, 36, 110), (4, 38, 100), (8, 36, 105), (12, 38, 100),
        (2, 42, 60), (6, 42, 62), (10, 42, 58), (14, 43, 70)]
BASS = [45, 48, 52, 55]

bar_idx = 0
last_report = time.monotonic()
next_spam = grid_start + 240  # play/stop panic every ~4 min

while time.monotonic() < end_time:
    bar_start = grid_start + bar_idx * bar

    # schedule this bar's traffic
    events = []
    for step, note, vel in DRUM:
        events.append((bar_start + step * s16 + random.uniform(-0.01, 0.01),
                       0x99, note, vel + random.randint(-8, 8)))
    root = BASS[bar_idx % 4]
    for step in (0, 6, 8, 12):
        t0 = bar_start + step * s16
        events.append((t0, 0x90, root, 96))
        events.append((t0 + 2.5 * s16, 0x80, root, 0))
    # a CC sweep through the bar (automation traffic on cutoff)
    for k in range(8):
        v = int(64 + 50 * random.random())
        events.append((bar_start + k * 2 * s16, 0xB0, 74, v))
    events.sort(key=lambda e: e[0])

    for t, st, d1, d2 in events:
        now = time.monotonic()
        if t > now:
            pump(t - now)  # read while waiting
        inject(st, d1, d2)

    bar_idx += 1

    # once per 8 bars: capture, and churn slots past 6 live tracks
    if bar_idx % 8 == 0:
        src = SRC_PADS if (bar_idx // 8) % 2 == 0 else SRC_KEYS
        send(frame(CMD_CAPTURE, bytes([src, 4])))
        if len(stats["tracks_live"]) > 6:
            send(frame(CMD_UNDO))
    # once per 5 bars: edit a random live track (toggle a cell)
    if bar_idx % 5 == 0 and stats["tracks_live"]:
        slot, gen = random.choice(stats["tracks_live"])
        tick = random.randrange(0, 16) * GRID
        send(frame(CMD_TRACK_EDIT,
                   bytes([slot, gen, 0, tick & 0xFF, tick >> 8,
                          36 + random.randrange(8), 100, 0, 0])))
        stats["edits"] += 1
    # once per 7 bars: scenes + groove/fx tweaks
    if bar_idx % 7 == 0:
        idx = random.randrange(4)
        send(frame(CMD_SCENE, bytes([0, idx])))  # save
        send(frame(CMD_SCENE, bytes([1, random.randrange(4)])))  # go
        stats["scenes"] += 1
        send(frame(CMD_GROOVE, bytes([2, random.randrange(50, 76)])))
        send(frame(CMD_FX, bytes([3, random.randrange(30, 90)])))

    # the panic test: play/stop/play spam, then re-anchor the grid
    if time.monotonic() > next_spam:
        print("  • play/stop spam burst")
        for _ in range(6):
            send(frame(CMD_STOP)); pump(0.08)
            send(frame(CMD_PLAY)); pump(0.08)
        send(frame(CMD_STOP)); pump(0.1)
        send(frame(CMD_REWIND)); pump(0.1)
        send(frame(CMD_PLAY))
        grid_start = time.monotonic() + bar + 0.01
        bar_idx = 0
        next_spam = time.monotonic() + 240

    if time.monotonic() - last_report > 60:
        last_report = time.monotonic()
        left = (end_time - time.monotonic()) / 60
        print(f"  • {left:.0f} min left — sent {stats['sent']}, "
              f"captures {stats['captures_ok']}, edits {stats['edits']}, "
              f"cpu max {stats['cpu_max']}%, "
              f"drops {stats['ring_drop_frames']}, "
              f"cksum {stats['checksum_errors']}, "
              f"reboots {stats['hello_reboots']}")

# --- teardown --------------------------------------------------------------------
for _ in range(len(stats["tracks_live"]) + 2):
    send(frame(CMD_UNDO))
    pump(0.1)
send(frame(CMD_STOP))
pump(0.5)

print("\n================ SOAK VERDICT ================")
ok = True


def check(cond, label, detail=""):
    global ok
    print(f"[{'PASS' if cond else 'FAIL'}] {label}"
          + (f" — {detail}" if detail else ""))
    ok = ok and cond


check(stats["hello_reboots"] == 0, "no reboots (the box never crashed)",
      f"{stats['hello_reboots']}")
check(stats["ring_drop_frames"] == 0, "no ring drops",
      f"{stats['ring_drop_frames']} MSG_STATS frames")
check(stats["checksum_errors"] == 0, "clean wire",
      f"{stats['checksum_errors']} checksum errors")
check(stats["cpu_max"] < 95, "CPU headroom held",
      f"peak {stats['cpu_max']}%")
check(stats["captures_refused"] == 0, "no unexpected capture refusals",
      f"{stats['captures_refused']}")
print(f"traffic: {stats['sent']} MIDI events, {stats['captures_ok']} captures, "
      f"{stats['edits']} edits, {stats['scenes']} scene ops")
print("==============================================")
os.close(fd)
sys.exit(0 if ok else 1)
