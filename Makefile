# Project Name
TARGET = GroovyDaisy

# Run from QSPI via the Daisy bootloader: the app outgrew the 128KB
# internal flash at Phase 2 (~96%). One-time setup on a fresh Pod:
#   1. hold BOOT, press RESET (DFU mode)
#   2. make program-boot        <- installs the Daisy bootloader
#   3. press RESET, then within the grace period: make program-dfu
# From then on, flashing is just: RESET, then make program-dfu while the
# bootloader LED is breathing.
APP_TYPE = BOOT_QSPI

# Sources
CPP_SOURCES = src/main.cpp

# Library Locations (override with `make LIBDAISY_DIR=...` if yours live elsewhere)
#
# PINNED to the libDaisy v5.4.0 checkout ("DaisyExamples 2", via the
# ~/Desktop/DaisyExamples2 symlink — make can't handle the space).
# libDaisy v7.x swapped the ST USB device middleware to submodules and its
# CDC never completes enumeration on macOS 14.5 (device visible, no
# /dev/cu.usbmodem created). Verified by A/B flashing 2026-07-05.
LIBDAISY_DIR ?= ../DaisyExamples2/libDaisy
DAISYSP_DIR ?= ../DaisyExamples2/DaisySP

# ReverbSc moved to DaisySP-LGPL upstream; the pinned checkout ships a
# prebuilt libdaisysp-lgpl.a and the core Makefile wires it on this flag
USE_DAISYSP_LGPL = 1

# Project includes
C_INCLUDES += -Isrc

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# Host-side unit tests (run on macOS, no toolchain needed)
test:
	$(MAKE) -C test
.PHONY: test
