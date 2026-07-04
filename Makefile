# Project Name
TARGET = GroovyDaisy

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
