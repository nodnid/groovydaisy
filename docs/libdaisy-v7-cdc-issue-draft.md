# DRAFT: libDaisy v7.x USB CDC never completes enumeration on macOS

Upstream issue draft for `electro-smith/libDaisy` (Phase 6 roadmap item).

**Before filing** (per daisy_hardware.md): reproduce once more against a
fresh `git clone --recurse-submodules` of current libDaisy master, to
rule out a skewed-submodule state in the local v7 checkout. If a fresh
clone enumerates fine, close this draft instead of filing.

---

## Title

USB CDC device never completes enumeration on macOS (v7.x, ST middleware
submodule era) — works on v5.4.0

## Body

**Hardware:** Daisy Seed (STM32H750, daisy bootloader v6_3) on a Daisy
Pod. CDC initialized with `usb_handle.Init(UsbHandle::FS_BOTH)` (also
reproduces with `FS_INTERNAL` alone).

**Host:** macOS 14.5 (Darwin 23.5.0), Apple Silicon.

**Symptom on v7.x** (checkout from Sep 2025, ST USB device middleware as
submodules): the device is visible in `system_profiler SPUSBDataType`
(VID 0x0483 present, descriptors readable), but **no interfaces are
published and no `/dev/cu.usbmodem*` node is ever created**. Console
shows configuration never completing. WebSerial/`termios` clients see
nothing to open.

**Same application code on v5.4.0** (exact pin: commit `85172e2b`,
"v5.4.0 + 22", 2024-01-08, verified stock): CDC enumerates instantly,
both ports, every boot.

**A/B verified** by flashing the identical application (only
`LIBDAISY_DIR` changed) back and forth on the same Seed, same cable,
same host, same session: v5.4.0 enumerates, v7.x does not
(2026-07-05).

One additional data point that may or may not be related: coming out of
the Daisy bootloader's own USB DFU into the app, we found the USB core
needed a full RCC force-reset before CDC init would complete even on
v5.4.0 (macOS-only wedge; descriptors readable, configuration never
completing — the same signature). Workaround in our app:

```cpp
__HAL_RCC_USB1_OTG_HS_FORCE_RESET();
__HAL_RCC_USB2_OTG_FS_FORCE_RESET();
System::Delay(10);
__HAL_RCC_USB1_OTG_HS_RELEASE_RESET();
__HAL_RCC_USB2_OTG_FS_RELEASE_RESET();
System::Delay(10);
usb_handle.Init(UsbHandle::FS_BOTH);
```

This workaround fixes the post-bootloader wedge on v5.4.0 but does NOT
fix v7.x enumeration.

**Repro:**
1. Any app calling `usb_handle.Init(UsbHandle::FS_INTERNAL)` +
   `SetReceiveCallback`, built against v7.x, `APP_TYPE = BOOT_QSPI`.
2. Flash via the Daisy bootloader, boot the app on a macOS 14.x host.
3. `ls /dev/cu.usbmodem*` → nothing; `system_profiler` shows the device.
4. Rebuild identical code against v5.4.0 → `/dev/cu.usbmodem*` appears
   within a second of boot.

Happy to test patches — this box's whole companion-app link is CDC, so
we have a fast A/B loop.
