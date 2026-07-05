# Electrosmith Daisy Seed / Daisy Pod / Daisy Bootloader — Hardware Reference

Compiled 2026-07 from official Electrosmith sources (docs.daisy.audio, product
datasheets/schematics, libDaisy source). Facts marked **[UNVERIFIED]** could
not be confirmed from an official source. Local note: our libDaisy checkout
(`../DaisyExamples/libDaisy`) ships bootloader **v6_3** binaries.

## 1. Daisy Seed — Core Hardware

- **MCU**: STM32H750IB, ARM Cortex-M7 @ **480 MHz**, **128 kB internal flash**.
- **External memory**: **64 MB SDRAM**, **8 MB QSPI flash** (memory-mapped at `0x90000000`).
- **Internal RAM regions** (libDaisy linker scripts):

  | Region | Size |
  |---|---|
  | FLASH (internal) | 128 kB |
  | DTCMRAM | 128 kB |
  | SRAM (D1 AXI) | 512 kB |
  | RAM_D2_DMA | 32 kB |
  | RAM_D2 | 256 kB |
  | RAM_D3 | 64 kB |
  | ITCMRAM | 64 kB |
  | SDRAM | 64 MB |
  | QSPIFLASH | 7936 kB (8 MB minus 256 kB reserved by the Daisy bootloader) |

- **Audio**: 96 kHz / 24-bit capable; libDaisy `SaiHandle` rates: 8/16/32/48/96 kHz (default 48 kHz).
- **Codec by Seed revision** (all pin-compatible): Rev 4 (2020–21) AK4556; Rev 5 (2021–23) WM8731; **Rev 7 (2023–) PCM3060** on SAI1 (PE2 MCLK, PE3 SD_B, PE4 FS, PE5 SCK, PE6 SD_A), hardware mode, 24-bit left-justified, no I2C control. Revision detect pins to GND: Rev 5 = PD3, Rev 7 = PD5.
- **I/O**: 31 GPIO; 12 ADC pins; 2× 12-bit DAC; SDMMC, SPI, UART, I2S, I2C, PWM. All GPIO 5V-tolerant **except** pins 24, 25, 28, 29, 30 (A2/D17, A3/D18, A6/D21, A7/D22, A8/D23 — 3.3 V only).
- **Power**: VIN accepts +5 to +17 V; safe to power from VIN and USB simultaneously. Onboard user LED on PC7.

## 2. USB Architecture (Seed + libDaisy)

The Seed has **two independent USB device paths**, both full-speed (12 Mbit/s):

| libDaisy `UsbHandle::UsbPeriph` | Physical connection | STM32H750 peripheral | Pins |
|---|---|---|---|
| `FS_INTERNAL` | **Onboard micro-USB on the Seed** | USB2 OTG_FS | PA11 = D−, PA12 = D+ |
| `FS_EXTERNAL` | **Header pins D29 (D−) / D30 (D+)**, ID on D0 | USB1 OTG_HS (on-chip FS PHY, FS speed only) | PB14 = D−, PB15 = D+ |
| `FS_BOTH` | Both simultaneously | Both | — |

- Both paths register the same USBD_CDC class with independent rx callbacks;
  `FS_BOTH` runs CDC on both connectors at once (GroovyDaisy does this since
  commit 5058e2e: app can connect via either port).
- USB **host** support (USB drives, USB-host MIDI) uses the external pins.

## 3. Daisy Pod — Board Hardware

USB-powered breakout carrying a socketed Seed. Current board rev 7
(databrief v1.1, May 2026); published schematic is Rev 5 (Oct 2022).

- **Controls**: 2 pots (POT_1 = D21/A6, POT_2 = D15/A0); clicked encoder
  (A = D26, B = D25, click = **D13**); 2 buttons (SW1 = D27, SW2 = D28);
  2 RGB LEDs (LED1 = D20/D19/D18, LED2 = D17/D24/D23).
- **Audio**: 3.5 mm stereo line-level **in**, 3.5 mm stereo line-level
  **out**, 3.5 mm **headphone** out via TPA6110 amp with its own analog
  volume pot (pot not readable in software).
- **microSD slot**: full 4-bit SDMMC1 (D1–D6); usable by the Daisy
  bootloader for firmware loading.
- **Expansion header**: SPI1 (D7–D10), I2C1 (D11/D12), A1/D16, A7/D22,
  3V3A, 3V3D, VIN, GND.

### 3.1 Pod USB port

**The Pod has its own onboard micro-USB connector.** Its D+/D− route
(through 0R jumpers + ESD array) to Seed pins **D30/D29** with ID on
**D0** — i.e. the Pod's connector is the Seed's **external USB =
`FS_EXTERNAL`** (USB1 OTG_HS, PB14/PB15). It also **powers the board**
(VBUS through a Schottky diode). The Seed's own micro-USB remains
accessible and powers everything too. Electrosmith notes computer USB
power can induce ground-loop hum; a USB power brick gives cleanest audio.

Practical: system DFU and default Daisy-bootloader DFU speak on the
**Seed's** port; the Pod's side port serves `FS_EXTERNAL` CDC or USB-host
media loading.

### 3.2 Pod MIDI

- **MIDI input only — no MIDI output.** One 3.5 mm **TRS MIDI in**,
  opto-isolated, into **USART1_RX = D14 (PB7)**. USART1_TX (D13/PB6) is
  consumed by the encoder click, so MIDI TX is impossible without rework.
  (Consequence for GroovyDaisy: SPEC's deferred "MIDI out" would need the
  expansion header + another UART, not the TRS jack.)
- **TRS type** selected by 0R populate/DNI jumpers (R78/R79/R83/R84);
  factory default reported **Type A** **[UNVERIFIED officially]**.

## 4. Daisy Bootloader

Two distinct bootloaders:

1. **STM32 system bootloader** (ROM): enter with **hold BOOT, press
   RESET**. DFU on the **Seed's onboard micro-USB** (`0483:df11`), can
   only write the 128 kB internal flash. Used by `make program-boot` and
   by `make program-dfu` when `APP_TYPE = BOOT_NONE`.
2. **Daisy bootloader** (lives in internal flash): installed once via
   **`make program-boot`** (from system-DFU mode). libDaisy ships
   `dsy_bootloader_v6_x-{intdfu,extdfu}-{10ms,2000ms}.bin`; default =
   intdfu-2000ms (DFU on the Seed's port, 2 s grace).

### APP_TYPE flow (core/Makefile)

| `APP_TYPE` | DFU write address | Runs from | Size limit |
|---|---|---|---|
| `BOOT_NONE` (default) | `0x08000000` | internal flash | 128 kB |
| `BOOT_SRAM` | `0x90040000` (QSPI) | SRAM/DTCM (full speed) | 480 kB |
| `BOOT_QSPI` | `0x90040000` (QSPI) | executes in place from QSPI (slower; I-cache mitigates) | ~7936 kB |

GroovyDaisy uses **BOOT_QSPI**. If CPU ever gets tight from QSPI XIP,
BOOT_SRAM is the escape hatch (we fit today at ~126 kB, but the default
SRAM linker places code in 128 kB DTCM — near our size; measure first).

### Runtime behavior

- **Grace period** on every boot: user LED "breathes" while the
  bootloader listens for DFU (~2 s on the default binary). **Pressing
  BOOT during the grace period extends it indefinitely** (a few rapid
  blinks acknowledge). With no valid app installed it waits forever.
- **Media loading**: scans SD card root (first), then a USB drive on the
  external port, for a `.bin`; flashes to QSPI only if different. Invalid
  binary → SOS blink pattern.
- **Handoff**: after the grace period it jumps to the app at
  `0x90040000` and is gone — the app owns all peripherals (its own USB
  CDC is unaffected). Since v6, the external-USB pins (D0/D29/D30) are
  left clean after handoff (matters to us: the Pod port is FS_EXTERNAL).
- **Re-enter from app**: `System::ResetToBootloader(BootloaderMode::DAISY)`
  (libDaisy ≥ 6.0).
- Bootloader v6+ requires libDaisy v5.3+.

## Sources

- Seed: https://docs.daisy.audio/hardware/Seed/ ·
  datasheet https://daisy.nyc3.cdn.digitaloceanspaces.com/products/seed/Daisy_Seed_datasheet.pdf ·
  pinout https://daisy.nyc3.cdn.digitaloceanspaces.com/products/seed/Daisy_Seed_pinout-25.pdf
- Pod: https://docs.daisy.audio/product/Daisy-Pod/ ·
  databrief https://daisy.nyc3.cdn.digitaloceanspaces.com/products/pod/Pod_databrief-5-14-26.pdf ·
  Rev5 schematic https://daisy.nyc3.cdn.digitaloceanspaces.com/products/pod/ES_Daisy_Pod_Rev5.pdf ·
  pinout https://daisy.nyc3.cdn.digitaloceanspaces.com/products/pod/Pod_pinout_26.pdf
- Bootloader guide: https://github.com/electro-smith/libDaisy/blob/master/doc/md/_a7_Getting-Started-Daisy-Bootloader.md
- libDaisy source: core/Makefile, src/hid/usb.{h,cpp}, src/usbd/usbd_conf.c,
  src/daisy_pod.cpp, src/per/sai.h, CHANGELOG.md
- (Non-official, TRS default only) MOD WIGGLER t=186808
