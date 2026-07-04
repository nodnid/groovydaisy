/**
 * Timing constants — single source of truth on the app side, mirroring
 * src/clock.h in the firmware. Do NOT redeclare these in components.
 */
export const PPQN = 96
export const BEATS_PER_BAR = 4
export const TICKS_PER_BAR = PPQN * BEATS_PER_BAR // 384

export const MIN_BPM = 60
export const MAX_BPM = 200

/** Protocol version this app speaks (must match firmware MSG_HELLO). */
export const PROTO_VER = 2

/** Fixed mixer strip ids (mirrors src/mixer.h). Track strips are 0..31. */
export const STRIP_GUITAR = 32
export const STRIP_SYNTH = 33
export const STRIP_DRUMS = 34
export const STRIP_METRO = 35
export const STRIP_MASTER = 0xff
