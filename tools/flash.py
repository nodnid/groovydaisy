#!/usr/bin/env python3
"""The hands-free flash ritual (works from firmware protocol v4+).

  1. Ask the running app to reboot into the Daisy bootloader
     (CMD_REBOOT_BOOTLOADER, magic-guarded, infinite DFU window)
  2. Wait for the DFU device to enumerate
  3. `make program-dfu`
  4. Wait for the CDC port to come back, request state, report the
     firmware HELLO (proto version) as proof of life

If no serial port answers (e.g. the box is already sitting in the
bootloader, or running pre-v4 firmware), falls through to plain
`make program-dfu` and tells you to do the RESET+BOOT dance.

Usage: python3 tools/flash.py   (from anywhere; builds nothing — run
`make` first if sources changed)
"""
import os, subprocess, sys, termios, time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT_CANDIDATES = ["/dev/cu.usbmodem5", "/dev/cu.usbmodem3973397D33331"]
CMD_REBOOT_BOOTLOADER, CMD_REQ_STATE, MSG_HELLO = 0x9F, 0x90, 0x01


def frame(t, payload=b""):
    hdr = bytes([t, len(payload) & 0xFF, len(payload) >> 8])
    cs = 0
    for b in hdr + payload:
        cs ^= b
    return bytes([0xAA]) + hdr + payload + bytes([cs])


def open_port(path):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    attrs[0] = attrs[1] = attrs[3] = 0
    attrs[2] |= termios.CLOCAL | termios.CREAD
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def dfu_present():
    try:
        out = subprocess.run(["dfu-util", "-l"], capture_output=True,
                             text=True, timeout=10).stdout
        return "Found DFU" in out
    except Exception:
        return False


# --- 1. ask the app to step down ---------------------------------------------
asked = False
for p in PORT_CANDIDATES:
    if not os.path.exists(p):
        continue
    try:
        fd = open_port(p)
        os.write(fd, frame(CMD_REBOOT_BOOTLOADER, bytes([0x44, 0x46])))
        os.close(fd)
        asked = True
        print(f"• asked the app on {p} to reboot into the bootloader")
        break
    except OSError:
        continue

if not asked:
    print("• no serial port answered — if the box isn't already in the")
    print("  bootloader, do the dance: RESET, then BOOT while breathing")

# --- 2. wait for DFU -----------------------------------------------------------
print("• waiting for DFU", end="", flush=True)
deadline = time.monotonic() + 15
while time.monotonic() < deadline:
    if dfu_present():
        print(" — there it is")
        break
    print(".", end="", flush=True)
    time.sleep(0.5)
else:
    sys.exit("\nno DFU device appeared — do the RESET+BOOT dance and rerun")

# --- 3. flash -------------------------------------------------------------------
r = subprocess.run(["make", "program-dfu"], cwd=REPO)
if r.returncode not in (0, 74):  # dfu-util exits 74 on the leave quirk
    sys.exit(f"flash failed ({r.returncode})")

# --- 4. proof of life ------------------------------------------------------------
print("• waiting for the app to wake", end="", flush=True)
deadline = time.monotonic() + 15
fd = None
while time.monotonic() < deadline and fd is None:
    for p in PORT_CANDIDATES:
        if os.path.exists(p):
            try:
                fd = open_port(p)
                break
            except OSError:
                pass
    if fd is None:
        print(".", end="", flush=True)
        time.sleep(0.5)
if fd is None:
    sys.exit("\nflashed, but no CDC port came back — power cycle?")

time.sleep(1.0)
os.write(fd, frame(CMD_REQ_STATE))
raw, hello = b"", None
deadline = time.monotonic() + 3
while time.monotonic() < deadline and hello is None:
    try:
        raw += os.read(fd, 4096)
    except (BlockingIOError, OSError):
        time.sleep(0.05)
        continue
    i = 0
    while i + 5 <= len(raw):
        if raw[i] != 0xAA:
            i += 1
            continue
        t, ln = raw[i + 1], raw[i + 2] | (raw[i + 3] << 8)
        if i + 5 + ln > len(raw):
            break
        if t == MSG_HELLO and ln >= 3:
            hello = (raw[i + 4], raw[i + 5], raw[i + 6])
        i += 5 + ln
    raw = raw[i:]
os.close(fd)

if hello:
    print(f"\n✓ alive: proto v{hello[0]}, firmware {hello[1]}.{hello[2]}")
else:
    print("\nflashed and enumerated, but no HELLO — check with the companion")
