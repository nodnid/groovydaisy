#!/usr/bin/env python3
"""Claude verifies lane editing (CMD_TRACK_EDIT) on real hardware.

Captures a small take, then edits it over serial and reads the track
data back after every operation:

  1. toggle a drum cell ON  -> event appears at the exact grid tick
  2. toggle the same cell   -> event gone (gen bumped again)
  3. move a synth note      -> on+off travel together, duration kept
  4. delete a synth note    -> on and its off both gone
  5. stale-gen edit         -> refused silently (no republish)

Cleans up with undo; leaves the stage as found.
"""
import os, sys, termios, time

PORT_CANDIDATES = ["/dev/cu.usbmodem5", "/dev/cu.usbmodem3973397D33331"]

CMD_PLAY, CMD_STOP, CMD_REWIND = 0x80, 0x81, 0x82
CMD_MIDI_INJECT, CMD_REQ_STATE = 0x8B, 0x90
CMD_CAPTURE, CMD_UNDO, CMD_TRACK_EDIT = 0xA0, 0xA1, 0xA8
MSG_TRANSPORT, MSG_TRACK, MSG_CAPTURE, MSG_TRACK_DATA = 0x02, 0x10, 0x12, 0x13
EDIT_TOGGLE, EDIT_DELETE, EDIT_MOVE = 0, 1, 2
SRC_PADS, SRC_KEYS = 0, 1
GRID, TPB = 24, 384


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
                inbox.append((t, bytes(body)))
                i += 5 + ln
            else:
                i += 1
        _raw = _raw[i:]
        if time.monotonic() >= end:
            return
        time.sleep(0.01)


def read_track_data(slot, req_gen=None, timeout=2.0):
    """Latest complete event list + gen for `slot` from MSG_TRACK_DATA.
    With req_gen set, explicitly requests the data first (the passive
    inbox may have been cleared since the commit pushed it)."""
    if req_gen is not None:
        send(frame(0xA5, bytes([slot, req_gen])))  # CMD_REQ_TRACK_DATA
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        pump(0.1)
        got, chunks_needed, gen = {}, None, None
        for t, p in inbox:
            if t != MSG_TRACK_DATA or len(p) < 4 or p[0] != slot:
                continue
            if gen is not None and p[1] != gen:
                got = {}
            gen = p[1]
            got[p[2]] = [(p[i] | (p[i + 1] << 8), p[i + 2], p[i + 3], p[i + 4])
                         for i in range(4, len(p) - 4, 5)]
            chunks_needed = p[3]
        if chunks_needed is not None and len(got) >= chunks_needed:
            events = []
            for idx in sorted(got):
                events.extend(got[idx])
            return gen, events
    return None, []


def edit(slot, gen, op, tick, note, a=0, b=0, c=0):
    send(frame(CMD_TRACK_EDIT,
               bytes([slot, gen, op, tick & 0xFF, tick >> 8, note, a, b, c])))


failures = []


def check(ok, label, detail=""):
    print(f"[{'PASS' if ok else 'FAIL'}] {label}" + (f" — {detail}" if detail else ""))
    if not ok:
        failures.append(label)


# --- setup: transport + tempo -------------------------------------------------
send(frame(CMD_STOP)); time.sleep(0.1)
send(frame(CMD_REWIND)); time.sleep(0.1)
send(frame(CMD_REQ_STATE))
pump(1.0)
tp = next((p for t, p in reversed(inbox) if t == MSG_TRANSPORT), None)
bpm = ((tp[2] | (tp[3] << 8)) / 10.0) if tp else 120.0
bar = 4 * 60.0 / bpm
print(f"• tempo {bpm:g} BPM")

send(frame(CMD_PLAY))
grid_start = time.monotonic() + bar + 0.01


def capture(source, at_bar):
    inbox.clear()
    while time.monotonic() < grid_start + at_bar * bar + 0.12:
        time.sleep(0.01)
    send(frame(CMD_CAPTURE, bytes([source, 1])))
    deadline = time.monotonic() + 2.0
    while time.monotonic() < deadline:
        pump(0.05)
        cap = next((p for t, p in inbox if t == MSG_CAPTURE and p[0] == 1), None)
        if cap:
            return cap[3], cap[4]  # slot, gen
    return None, None


# Bar 0: two pad hits; bar 0 also: one synth note
t_hit = grid_start + 0.2
while time.monotonic() < t_hit:
    time.sleep(0.01)
send(frame(CMD_MIDI_INJECT, bytes([0x99, 36, 100])))
send(frame(CMD_MIDI_INJECT, bytes([0x90, 60, 100])))
time.sleep(0.2)
send(frame(CMD_MIDI_INJECT, bytes([0x80, 60, 0])))
time.sleep(0.3)
send(frame(CMD_MIDI_INJECT, bytes([0x99, 38, 100])))

drum_slot, drum_gen = capture(SRC_PADS, 1)
keys_slot, keys_gen = capture(SRC_KEYS, 1)
check(drum_slot is not None and keys_slot is not None, "both takes captured",
      f"drums slot {drum_slot}, keys slot {keys_slot}")
if drum_slot is None or keys_slot is None:
    send(frame(CMD_STOP))
    sys.exit(1)

# --- 1+2. drum toggle on / off ------------------------------------------------
gen, ev0 = read_track_data(drum_slot, req_gen=drum_gen)
if gen is None:
    check(False, "drum track data readable")
    send(frame(CMD_UNDO)); send(frame(CMD_UNDO)); send(frame(CMD_STOP))
    sys.exit(1)
n0 = len(ev0)
inbox.clear()
edit(drum_slot, gen, EDIT_TOGGLE, 8 * GRID, 42, 90)  # hat on step 8
gen1, ev1 = read_track_data(drum_slot)
added = [e for e in ev1 if e[0] == 8 * GRID and e[2] == 42]
check(gen1 == (gen + 1) % 256 and len(ev1) == n0 + 1 and len(added) == 1,
      "drum cell toggles ON at the grid tick",
      f"gen {gen}->{gen1}, events {n0}->{len(ev1)}")

inbox.clear()
edit(drum_slot, gen1, EDIT_TOGGLE, 8 * GRID, 42, 90)
gen2, ev2 = read_track_data(drum_slot)
check(len(ev2) == n0 and not [e for e in ev2 if e[0] == 8 * GRID and e[2] == 42],
      "same cell toggles OFF", f"events {len(ev1)}->{len(ev2)}")

# --- 3. move the synth note ----------------------------------------------------
gen, kev = read_track_data(keys_slot, req_gen=keys_gen)
if gen is None:
    check(False, "keys track data readable")
    send(frame(CMD_UNDO)); send(frame(CMD_UNDO)); send(frame(CMD_STOP))
    sys.exit(1)
ons = [e for e in kev if (e[1] & 0xF0) == 0x90 and e[3] > 0 and e[2] == 60]
check(len(ons) == 1, "synth take has the note", f"ons {ons}")
if ons:
    on_tick = ons[0][0]
    offs = [e for e in kev if e[2] == 60 and
            ((e[1] & 0xF0) == 0x80 or ((e[1] & 0xF0) == 0x90 and e[3] == 0))]
    dur = (offs[0][0] - on_tick) % TPB if offs else -1
    new_tick = (on_tick + 4 * GRID) % TPB
    inbox.clear()
    # a,b = new_tick LE, c = new note
    edit(keys_slot, gen, EDIT_MOVE, on_tick, 60,
         new_tick & 0xFF, new_tick >> 8, 64)
    gen3, kev3 = read_track_data(keys_slot)
    ons3 = [e for e in kev3 if (e[1] & 0xF0) == 0x90 and e[3] > 0 and e[2] == 64]
    offs3 = [e for e in kev3 if e[2] == 64 and
             ((e[1] & 0xF0) == 0x80 or ((e[1] & 0xF0) == 0x90 and e[3] == 0))]
    dur3 = (offs3[0][0] - ons3[0][0]) % TPB if ons3 and offs3 else -2
    check(len(ons3) == 1 and ons3[0][0] == new_tick and dur3 == dur,
          "synth note moves with its off (duration kept)",
          f"tick {on_tick}->{ons3[0][0] if ons3 else '?'}, dur {dur}=={dur3}")

    # --- 4. delete it ---------------------------------------------------------
    inbox.clear()
    edit(keys_slot, gen3, EDIT_DELETE, new_tick, 64)
    gen4, kev4 = read_track_data(keys_slot)
    left = [e for e in kev4 if e[2] == 64]
    check(len(left) == 0, "delete removes on AND off", f"remaining {left}")

    # --- 5. stale gen refused ---------------------------------------------------
    inbox.clear()
    edit(keys_slot, gen3, EDIT_TOGGLE, 0, 36, 100)  # gen3 is stale now
    pump(0.6)
    republished = [p for t, p in inbox if t == MSG_TRACK and p[0] == keys_slot]
    check(len(republished) == 0, "stale-gen edit is refused silently")

# --- cleanup -------------------------------------------------------------------
send(frame(CMD_UNDO)); time.sleep(0.2)
send(frame(CMD_UNDO)); time.sleep(0.2)
send(frame(CMD_STOP))

print("=========================================")
if failures:
    print("FAILURES:")
    for f in failures:
        print(f"  ✗ {f}")
    sys.exit(1)
print("The lanes are an instrument now. 🎩")
os.close(fd)
