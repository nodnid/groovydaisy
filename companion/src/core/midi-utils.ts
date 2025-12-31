/**
 * MIDI Utilities for GroovyDaisy Companion
 *
 * Parses MIDI status bytes and converts to human-readable format.
 */

// Note names for conversion
const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']

/**
 * Convert MIDI note number to name (e.g., 60 -> "C4")
 */
export function noteToName(note: number): string {
  const octave = Math.floor(note / 12) - 1
  const noteName = NOTE_NAMES[note % 12]
  return `${noteName}${octave}`
}

/**
 * MIDI message types
 */
export type MidiMessageType =
  | 'Note On'
  | 'Note Off'
  | 'CC'
  | 'Pitch Bend'
  | 'Aftertouch'
  | 'Poly AT'
  | 'Program'
  | 'Unknown'

/**
 * Parsed MIDI message
 */
export interface ParsedMidiMessage {
  type: MidiMessageType
  channel: number
  data: string
  raw: { status: number; data1: number; data2: number }
}

/**
 * Parse MIDI status byte to get message type
 */
export function getMessageType(status: number): MidiMessageType {
  const type = status & 0xf0
  switch (type) {
    case 0x90:
      return 'Note On'
    case 0x80:
      return 'Note Off'
    case 0xb0:
      return 'CC'
    case 0xe0:
      return 'Pitch Bend'
    case 0xd0:
      return 'Aftertouch'
    case 0xa0:
      return 'Poly AT'
    case 0xc0:
      return 'Program'
    default:
      return 'Unknown'
  }
}

/**
 * Parse raw MIDI bytes into human-readable format
 */
export function parseMidiMessage(
  status: number,
  data1: number,
  data2: number
): ParsedMidiMessage {
  const type = getMessageType(status)
  const channel = (status & 0x0f) + 1

  let data = ''

  switch (type) {
    case 'Note On':
      // Note On with velocity 0 is actually Note Off
      if (data2 === 0) {
        return {
          type: 'Note Off',
          channel,
          data: noteToName(data1),
          raw: { status, data1, data2 },
        }
      }
      data = `${noteToName(data1)} vel=${data2}`
      break

    case 'Note Off':
      data = noteToName(data1)
      break

    case 'CC':
      data = `${data1} = ${data2}`
      break

    case 'Pitch Bend':
      // Convert 14-bit pitch bend to signed value (-8192 to +8191)
      const bend = (data2 << 7) | data1
      const centered = bend - 8192
      data = `${centered > 0 ? '+' : ''}${centered}`
      break

    case 'Aftertouch':
      data = `${data1}`
      break

    case 'Poly AT':
      data = `${noteToName(data1)} = ${data2}`
      break

    case 'Program':
      data = `${data1}`
      break

    default:
      data = `${data1.toString(16).padStart(2, '0')} ${data2.toString(16).padStart(2, '0')}`
  }

  return {
    type,
    channel,
    data,
    raw: { status, data1, data2 },
  }
}

/**
 * Common CC names for display
 */
export const CC_NAMES: Record<number, string> = {
  1: 'Mod Wheel',
  7: 'Volume',
  10: 'Pan',
  11: 'Expression',
  64: 'Sustain',
  74: 'Cutoff',
  71: 'Resonance',
  73: 'Attack',
  75: 'Decay',
  76: 'Osc1 Wave',
  77: 'Osc2 Wave',
  // KeyLab encoders
  93: 'Enc 5',
  18: 'Enc 6',
  19: 'Enc 7',
  16: 'Enc 8',
  17: 'Enc 9',
  // KeyLab faders
  79: 'Fader 3',
  72: 'Fader 4',
  80: 'Fader 5',
  81: 'Fader 6',
  82: 'Fader 7',
  83: 'Fader 8',
  85: 'Fader 9',
}

/**
 * Get CC name if known, otherwise return number
 */
export function getCCName(cc: number): string {
  return CC_NAMES[cc] ?? `CC ${cc}`
}

/**
 * Format CC message with name
 */
export function formatCC(cc: number, value: number): string {
  const name = CC_NAMES[cc]
  if (name) {
    return `${name} (${cc}) = ${value}`
  }
  return `CC ${cc} = ${value}`
}
