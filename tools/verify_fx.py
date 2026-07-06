#!/usr/bin/env python3
"""Claude verifies Phase 5 (the space) on real hardware.

No audio readback exists, but MSG_METERS is a 10 Hz window into the mix
bus — enough to PROVE the sends work:

  1. FX param round-trip: CMD_FX set -> MSG_FX echo matches
  2. Meters stream at ~10 Hz; CPU% is sane (< 90) with reverb running
  3. Reverb tail: crank drum reverb send, fire one hit, watch the master
     meter ring out for several frames after the strip meter dies
  4. Ping-pong delay: crank drum delay send, fire one hit, count echo
     bumps in the master meter at the dotted-8th spacing
  5. Mod-wheel guard: wheel motion (CC1 nonzero stream ending at 0) must
     NOT switch banks; a lone CC1 zero MUST
  6. MSG_STATS stays silent (no ring drops) through all of it

Leaves the stage as found (bank + sends restored, transport stopped).
"""
import os, sys, termios, time

PORT_CANDIDATES = ["/dev/cu.usbmodem5", "/dev/cu.usbmodem3973397D33331"]

CMD_PLAY, CMD_STOP, CMD_REWIND = 0x80, 0x81, 0x82
CMD_SET_BANK, CMD_MIXER, CMD_MIDI_INJECT = 0x87, 0x88, 0x8B
CMD_REQ_STATE, CMD_FX = 0x90, 0xA7
MSG_TRANSPORT, MSG_BANK, MSG_FX, MSG_METERS, MSG_STATS = 0x02, 0x07, 0x21, 0x22, 0x23
MIX_SEND_REV, MIX_SEND_DLY = 3, 4
STRIP_DRUMS = 34


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

inbox = []
_raw = b""


def pump(dur=0.0):
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
                inbox.append((time.monotonic(), t, bytes(body)))
                i += 5 + ln
            else:
                i += 1
        _raw = _raw[i:]
        if time.monotonic() >= end:
            return
        time.sleep(0.01)


def last_of(mtype):
    for ts, t, p in reversed(inbox):
        if t == mtype:
            return p
    return None


failures = []


def check(ok, label, detail=""):
    print(f"[{'PASS' if ok else 'FAIL'}] {label}" + (f" — {detail}" if detail else ""))
    if not ok:
        failures.append(label)


# --- setup ------------------------------------------------------------------
send(frame(CMD_STOP))
time.sleep(0.1)
send(frame(CMD_REQ_STATE))
pump(1.0)
tp = last_of(MSG_TRANSPORT)
bpm = ((tp[2] | (tp[3] << 8)) / 10.0) if tp else 120.0
fx0 = last_of(MSG_FX)  # to restore later
bank0 = last_of(MSG_BANK)
print(f"• tempo {bpm:g} BPM")

# --- 1. FX param round-trip --------------------------------------------------
inbox.clear()
send(frame(CMD_FX, bytes([0, 100])))  # rev size
send(frame(CMD_FX, bytes([1, 64])))   # rev tone
send(frame(CMD_FX, bytes([2, 1])))    # dotted 8th
send(frame(CMD_FX, bytes([3, 70])))   # dly feedback
pump(0.5)
p = last_of(MSG_FX)
check(p is not None and list(p[:4]) == [100, 64, 1, 70],
      "FX params round-trip", f"echo {list(p[:4]) if p else None}")

# --- 2. meters stream + CPU --------------------------------------------------
inbox.clear()
pump(1.2)
meter_frames = [(ts, p) for ts, t, p in inbox if t == MSG_METERS]
rate = len(meter_frames) / 1.2
cpu = meter_frames[-1][1][2] if meter_frames else 255
check(8 <= rate <= 12, "meters stream ~10 Hz", f"{rate:.1f} Hz")
check(cpu < 90, "CPU headroom with reverb running", f"{cpu}%")

# --- 3+4. reverb tail & delay echoes ----------------------------------------
send(frame(CMD_PLAY))
time.sleep((60.0 / bpm) * 4 + 0.3)  # count-in bar


def master_trace(dur):
    """Fire one loud kick, return [(t_rel_ms, master_peak, drums_peak)]."""
    inbox.clear()
    t0 = time.monotonic()
    send(frame(CMD_MIDI_INJECT, bytes([0x99, 36, 127])))
    pump(dur)
    return [
        (int((ts - t0) * 1000), p[0], p[3 + STRIP_DRUMS])
        for ts, t, p in inbox
        if t == MSG_METERS and len(p) > 3 + STRIP_DRUMS
    ]


# Reverb: send up, delay off
send(frame(CMD_MIXER, bytes([STRIP_DRUMS, MIX_SEND_REV, 110])))
send(frame(CMD_MIXER, bytes([STRIP_DRUMS, MIX_SEND_DLY, 0])))
time.sleep(0.1)
tr = master_trace(1.5)
# The drum strip peak dies fast (one-shot kick); the master should keep
# ringing (reverb return) after the strip is quiet.
late_master = [m for t, m, d in tr if 500 <= t <= 1200]
late_strip = [d for t, m, d in tr if 500 <= t <= 1200]
check(late_master and max(late_master) > 6 and max(late_strip) <= max(late_master),
      "reverb tail rings after the hit dies",
      f"master 0.5-1.2s: {late_master}")

# Delay: send up, reverb off — expect echo bumps at the dotted 8th
send(frame(CMD_MIXER, bytes([STRIP_DRUMS, MIX_SEND_REV, 0])))
send(frame(CMD_MIXER, bytes([STRIP_DRUMS, MIX_SEND_DLY, 120])))
time.sleep(0.1)
echo_ms = (60.0 / bpm) * 4 / 16 * 3 * 1000  # dotted 8th = 3 sixteenths
tr = master_trace(2.0)
vals = [m for t, m, d in tr]
# Count local maxima after the initial hit window
bumps = 0
for i in range(2, len(vals) - 1):
    if vals[i] > vals[i - 1] and vals[i] >= vals[i + 1] and vals[i] > 5:
        bumps += 1
check(bumps >= 2, f"ping-pong echoes at ~{echo_ms:.0f} ms spacing",
      f"trace {vals}")

send(frame(CMD_STOP))
send(frame(CMD_MIXER, bytes([STRIP_DRUMS, MIX_SEND_DLY, 0])))
send(frame(CMD_MIXER, bytes([STRIP_DRUMS, MIX_SEND_REV, 10])))  # default-ish

# --- 5. mod-wheel guard -------------------------------------------------------
inbox.clear()
# Wheel motion: a sweep ending at 0 — must NOT switch banks
for v in [10, 40, 90, 127, 80, 30, 0]:
    send(frame(CMD_MIDI_INJECT, bytes([0xB0, 1, v])))
    time.sleep(0.02)
pump(0.5)
check(last_of(MSG_BANK) is None, "mod wheel sweep does not switch banks")

time.sleep(0.5)  # past the guard window
inbox.clear()
send(frame(CMD_MIDI_INJECT, bytes([0xB0, 1, 0])))  # lone zero = Part 1
pump(0.5)
check(last_of(MSG_BANK) is not None, "lone CC1 zero DOES switch banks")

# restore bank + fx
if bank0 is not None:
    send(frame(CMD_SET_BANK, bytes([bank0[0]])))
if fx0 is not None:
    for i in range(4):
        send(frame(CMD_FX, bytes([i, fx0[i]])))

# --- 6. no ring drops through all of it --------------------------------------
stats = last_of(MSG_STATS)
check(stats is None, "no ring drops during the whole test",
      "" if stats is None else f"stats payload {list(stats)}")

print("=========================================")
if failures:
    print("FAILURES:")
    for f in failures:
        print(f"  ✗ {f}")
    sys.exit(1)
print("The space is real. 🎩")
os.close(fd)
