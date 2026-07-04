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
LIBDAISY_DIR ?= ../DaisyExamples/libDaisy
DAISYSP_DIR ?= ../DaisyExamples/DaisySP

# Project includes
C_INCLUDES += -Isrc

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# Host-side unit tests (run on macOS, no toolchain needed)
test:
	$(MAKE) -C test
.PHONY: test
