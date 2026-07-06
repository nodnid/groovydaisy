#pragma once
#ifndef GROOVYDAISY_ITCM_H
#define GROOVYDAISY_ITCM_H

/**
 * ITCM placement for hot DSP code (see ROADMAP "the CPU budget"): the
 * app executes XIP from QSPI and the audio callback's working set
 * overflows the 16 KB I-cache, so hot loops fetch from ~100 MHz quad
 * SPI continuously. Functions marked ITCM_TEXT are linked into the
 * 64 KB zero-wait ITCM (STM32H750IB_qspi_itcm.lds) and copied there by
 * main() before audio starts. No-op on host builds (mach-o has no such
 * section syntax; the tests only care about behavior).
 */
#if defined(__arm__)
// Two flavors because GCC refuses weak (inline/member) and strong
// definitions in one named section; the linker wildcard .itcm_text*
// collects both.
#define ITCM_TEXT __attribute__((section(".itcm_text")))
#define ITCM_INLINE __attribute__((section(".itcm_text_w")))
#else
#define ITCM_TEXT
#define ITCM_INLINE
#endif

#endif // GROOVYDAISY_ITCM_H
