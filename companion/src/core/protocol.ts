/**
 * GroovyDaisy Binary Protocol Parser
 *
 * Message format: [SYNC][TYPE][LEN_LO][LEN_HI][PAYLOAD...][CHECKSUM]
 *
 * SYNC:     0xAA - Start of message marker
 * TYPE:     Message type
 * LEN:      16-bit payload length (little-endian)
 * PAYLOAD:  Variable length data
 * CHECKSUM: XOR of all bytes from TYPE through PAYLOAD
 */

// Sync byte
export const SYNC_BYTE = 0xaa

// Message types: Daisy -> Companion
export const MSG_TICK = 0x01
export const MSG_TRANSPORT = 0x02
export const MSG_VOICES = 0x03
export const MSG_MIDI_IN = 0x04
export const MSG_CC_STATE = 0x05
export const MSG_SYNTH_STATE = 0x06
export const MSG_CC_BANK = 0x07
export const MSG_FADER_STATE = 0x08
export const MSG_MIXER_STATE = 0x09
export const MSG_DEBUG = 0xff

// Message types: Companion -> Daisy
export const CMD_PLAY = 0x80
export const CMD_STOP = 0x81
export const CMD_RECORD = 0x82
export const CMD_TEMPO = 0x83
export const CMD_PATTERN = 0x84
export const CMD_SYNTH_PARAM = 0x85
export const CMD_LOAD_PRESET = 0x86
export const CMD_SET_BANK = 0x87
export const CMD_REQ_STATE = 0x90
export const CMD_REQ_SYNTH = 0x92

// Synth parameter IDs (must match synth.h ParamId enum)
export enum SynthParamId {
  OSC1_WAVE = 0,
  OSC2_WAVE,
  OSC1_LEVEL,
  OSC2_LEVEL,
  OSC2_DETUNE,
  FILTER_CUTOFF,
  FILTER_RES,
  FILTER_ENV_AMT,
  AMP_ATTACK,
  AMP_DECAY,
  AMP_SUSTAIN,
  AMP_RELEASE,
  FILT_ATTACK,
  FILT_DECAY,
  FILT_SUSTAIN,
  FILT_RELEASE,
  VEL_TO_AMP,
  VEL_TO_FILTER,
  LEVEL,
}

// Waveform names
export const WAVEFORM_NAMES = ['Sine', 'Triangle', 'Saw', 'Square']

// Factory preset names
export const FACTORY_PRESETS = ['Init Patch', 'Warm Pad', 'Pluck Lead', 'Bass']

// Parsed message types
export interface TickMessage {
  type: typeof MSG_TICK
  tick: number
}

export interface TransportMessage {
  type: typeof MSG_TRANSPORT
  playing: boolean
  recording: boolean
  bpm: number
}

export interface VoicesMessage {
  type: typeof MSG_VOICES
  synth: number
  drums: number
}

export interface MidiInMessage {
  type: typeof MSG_MIDI_IN
  status: number
  data1: number
  data2: number
}

export interface DebugMessage {
  type: typeof MSG_DEBUG
  text: string
}

export interface CCBankMessage {
  type: typeof MSG_CC_BANK
  bank: number  // 0-3
}

export interface FaderStateMessage {
  type: typeof MSG_FADER_STATE
  states: Array<{ pickedUp: boolean; needsPickup: boolean }>  // 9 faders
}

export interface MixerStateMessage {
  type: typeof MSG_MIXER_STATE
  drumLevels: number[]   // 8 values, 0-127
  drumPans: number[]     // 8 values, 0-127
  drumMaster: number     // 0-127
  synthLevel: number     // 0-127
  synthPan: number       // 0-127
  synthMaster: number    // 0-127
  masterOut: number      // 0-127
}

// Synth parameters - mirrors SynthParams struct in synth.h
export interface SynthParams {
  osc1Wave: number
  osc2Wave: number
  osc1Level: number
  osc2Level: number
  osc2Detune: number

  filterCutoff: number
  filterRes: number
  filterEnvAmt: number

  ampAttack: number
  ampDecay: number
  ampSustain: number
  ampRelease: number

  filtAttack: number
  filtDecay: number
  filtSustain: number
  filtRelease: number

  velToAmp: number
  velToFilter: number

  level: number
}

export interface SynthStateMessage {
  type: typeof MSG_SYNTH_STATE
  params: SynthParams
  presetIndex: number
}

export type ParsedMessage =
  | TickMessage
  | TransportMessage
  | VoicesMessage
  | MidiInMessage
  | DebugMessage
  | SynthStateMessage
  | CCBankMessage
  | FaderStateMessage
  | MixerStateMessage

// Parser state
enum ParserState {
  WAIT_SYNC,
  WAIT_TYPE,
  WAIT_LEN_LO,
  WAIT_LEN_HI,
  WAIT_PAYLOAD,
  WAIT_CHECKSUM,
}

const MAX_PAYLOAD = 256

/**
 * Calculate XOR checksum over a buffer
 */
function calculateChecksum(data: Uint8Array): number {
  let sum = 0
  for (let i = 0; i < data.length; i++) {
    sum ^= data[i]
  }
  return sum
}

/**
 * Read a little-endian float from a buffer at the given offset
 */
function readFloat(data: Uint8Array, offset: number): number {
  const view = new DataView(data.buffer, data.byteOffset + offset, 4)
  return view.getFloat32(0, true) // little-endian
}

/**
 * Write a little-endian float to a buffer
 */
function writeFloat(value: number): Uint8Array {
  const buf = new Uint8Array(4)
  const view = new DataView(buf.buffer)
  view.setFloat32(0, value, true) // little-endian
  return buf
}

/**
 * Build a command message to send to Daisy
 */
export function buildMessage(type: number, payload: Uint8Array = new Uint8Array(0)): Uint8Array {
  const msg = new Uint8Array(5 + payload.length)
  msg[0] = SYNC_BYTE
  msg[1] = type
  msg[2] = payload.length & 0xff
  msg[3] = (payload.length >> 8) & 0xff

  if (payload.length > 0) {
    msg.set(payload, 4)
  }

  // Checksum covers TYPE + LEN + PAYLOAD
  const checksumData = new Uint8Array(3 + payload.length)
  checksumData[0] = type
  checksumData[1] = payload.length & 0xff
  checksumData[2] = (payload.length >> 8) & 0xff
  checksumData.set(payload, 3)

  msg[4 + payload.length] = calculateChecksum(checksumData)

  return msg
}

/**
 * Build a synth parameter command
 */
export function buildSynthParamCommand(paramId: SynthParamId, value: number): Uint8Array {
  const payload = new Uint8Array(5)
  payload[0] = paramId
  payload.set(writeFloat(value), 1)
  return buildMessage(CMD_SYNTH_PARAM, payload)
}

/**
 * Build a load preset command
 */
export function buildLoadPresetCommand(presetIndex: number): Uint8Array {
  return buildMessage(CMD_LOAD_PRESET, new Uint8Array([presetIndex]))
}

/**
 * Build a request synth state command
 */
export function buildRequestSynthCommand(): Uint8Array {
  return buildMessage(CMD_REQ_SYNTH)
}

/**
 * Build a set bank command
 */
export function buildSetBankCommand(bank: number): Uint8Array {
  return buildMessage(CMD_SET_BANK, new Uint8Array([bank & 0x03]))
}

/**
 * Get default synth params (matches Init Patch)
 */
export function getDefaultSynthParams(): SynthParams {
  return {
    osc1Wave: 2, // Saw
    osc2Wave: 3, // Square
    osc1Level: 1.0,
    osc2Level: 0.5,
    osc2Detune: 0,

    filterCutoff: 2000,
    filterRes: 0.3,
    filterEnvAmt: 0.5,

    ampAttack: 0.01,
    ampDecay: 0.2,
    ampSustain: 0.7,
    ampRelease: 0.3,

    filtAttack: 0.01,
    filtDecay: 0.3,
    filtSustain: 0.3,
    filtRelease: 0.3,

    velToAmp: 0.5,
    velToFilter: 0.3,

    level: 0.7,
  }
}

/**
 * Parse a message payload into a typed message object
 */
function parsePayload(type: number, payload: Uint8Array): ParsedMessage | null {
  switch (type) {
    case MSG_TICK:
      if (payload.length >= 4) {
        const tick =
          payload[0] | (payload[1] << 8) | (payload[2] << 16) | (payload[3] << 24)
        return { type: MSG_TICK, tick: tick >>> 0 } // >>> 0 ensures unsigned
      }
      break

    case MSG_TRANSPORT:
      if (payload.length >= 4) {
        return {
          type: MSG_TRANSPORT,
          playing: payload[0] !== 0,
          recording: payload[1] !== 0,
          bpm: payload[2] | (payload[3] << 8),
        }
      }
      break

    case MSG_VOICES:
      if (payload.length >= 2) {
        return {
          type: MSG_VOICES,
          synth: payload[0],
          drums: payload[1],
        }
      }
      break

    case MSG_MIDI_IN:
      if (payload.length >= 3) {
        return {
          type: MSG_MIDI_IN,
          status: payload[0],
          data1: payload[1],
          data2: payload[2],
        }
      }
      break

    case MSG_DEBUG:
      return {
        type: MSG_DEBUG,
        text: new TextDecoder().decode(payload),
      }

    case MSG_SYNTH_STATE:
      // Parse synth state - must match SendSynthState() order in GroovyDaisy.cpp
      if (payload.length >= 60) {
        let idx = 0

        const params: SynthParams = {
          // Oscillators
          osc1Wave: payload[idx++],
          osc2Wave: payload[idx++],
          osc1Level: readFloat(payload, idx),
          osc2Level: (idx += 4, readFloat(payload, idx)),
          osc2Detune: (idx += 4, payload[idx++] - 24), // Convert from unsigned offset

          // Filter
          filterCutoff: readFloat(payload, idx),
          filterRes: (idx += 4, readFloat(payload, idx)),
          filterEnvAmt: (idx += 4, readFloat(payload, idx)),

          // Amp envelope
          ampAttack: (idx += 4, readFloat(payload, idx)),
          ampDecay: (idx += 4, readFloat(payload, idx)),
          ampSustain: (idx += 4, readFloat(payload, idx)),
          ampRelease: (idx += 4, readFloat(payload, idx)),

          // Filter envelope
          filtAttack: (idx += 4, readFloat(payload, idx)),
          filtDecay: (idx += 4, readFloat(payload, idx)),
          filtSustain: (idx += 4, readFloat(payload, idx)),
          filtRelease: (idx += 4, readFloat(payload, idx)),

          // Velocity sensitivity
          velToAmp: (idx += 4, readFloat(payload, idx)),
          velToFilter: (idx += 4, readFloat(payload, idx)),

          // Master level
          level: (idx += 4, readFloat(payload, idx)),
        }

        idx += 4
        const presetIndex = payload[idx]

        return {
          type: MSG_SYNTH_STATE,
          params,
          presetIndex,
        }
      }
      break

    case MSG_CC_BANK:
      if (payload.length >= 1) {
        return {
          type: MSG_CC_BANK,
          bank: payload[0],
        }
      }
      break

    case MSG_FADER_STATE:
      if (payload.length >= 9) {
        const states = []
        for (let i = 0; i < 9; i++) {
          states.push({
            pickedUp: (payload[i] & 0x01) !== 0,
            needsPickup: (payload[i] & 0x02) !== 0,
          })
        }
        return {
          type: MSG_FADER_STATE,
          states,
        }
      }
      break

    case MSG_MIXER_STATE:
      // [drum_levels:8][drum_pans:8][drum_master:1][synth_level:1][synth_pan:1][synth_master:1][master_out:1]
      if (payload.length >= 21) {
        const drumLevels = Array.from(payload.slice(0, 8))
        const drumPans = Array.from(payload.slice(8, 16))
        return {
          type: MSG_MIXER_STATE,
          drumLevels,
          drumPans,
          drumMaster: payload[16],
          synthLevel: payload[17],
          synthPan: payload[18],
          synthMaster: payload[19],
          masterOut: payload[20],
        }
      }
      break
  }

  return null
}

/**
 * Streaming protocol parser
 */
export class ProtocolParser {
  private state: ParserState = ParserState.WAIT_SYNC
  private type: number = 0
  private payloadLen: number = 0
  private payloadIdx: number = 0
  private payload: Uint8Array = new Uint8Array(MAX_PAYLOAD)
  private runningChecksum: number = 0
  private onMessage: (msg: ParsedMessage) => void
  private onChecksumError: (() => void) | null = null

  constructor(
    onMessage: (msg: ParsedMessage) => void,
    onChecksumError?: () => void
  ) {
    this.onMessage = onMessage
    this.onChecksumError = onChecksumError ?? null
  }

  /**
   * Reset parser state
   */
  reset(): void {
    this.state = ParserState.WAIT_SYNC
    this.type = 0
    this.payloadLen = 0
    this.payloadIdx = 0
    this.runningChecksum = 0
  }

  /**
   * Feed raw bytes to the parser
   */
  feed(data: Uint8Array): void {
    for (let i = 0; i < data.length; i++) {
      this.feedByte(data[i])
    }
  }

  /**
   * Feed a single byte to the parser
   */
  private feedByte(byte: number): void {
    switch (this.state) {
      case ParserState.WAIT_SYNC:
        if (byte === SYNC_BYTE) {
          this.state = ParserState.WAIT_TYPE
          this.runningChecksum = 0
        }
        break

      case ParserState.WAIT_TYPE:
        this.type = byte
        this.runningChecksum ^= byte
        this.state = ParserState.WAIT_LEN_LO
        break

      case ParserState.WAIT_LEN_LO:
        this.payloadLen = byte
        this.runningChecksum ^= byte
        this.state = ParserState.WAIT_LEN_HI
        break

      case ParserState.WAIT_LEN_HI:
        this.payloadLen |= byte << 8
        this.runningChecksum ^= byte
        this.payloadIdx = 0

        if (this.payloadLen > MAX_PAYLOAD) {
          // Invalid length, reset
          this.reset()
        } else if (this.payloadLen === 0) {
          this.state = ParserState.WAIT_CHECKSUM
        } else {
          this.state = ParserState.WAIT_PAYLOAD
        }
        break

      case ParserState.WAIT_PAYLOAD:
        this.payload[this.payloadIdx++] = byte
        this.runningChecksum ^= byte

        if (this.payloadIdx >= this.payloadLen) {
          this.state = ParserState.WAIT_CHECKSUM
        }
        break

      case ParserState.WAIT_CHECKSUM:
        if (byte === this.runningChecksum) {
          // Valid message - parse and dispatch
          const payloadSlice = this.payload.slice(0, this.payloadLen)
          const msg = parsePayload(this.type, payloadSlice)
          if (msg) {
            this.onMessage(msg)
          }
        } else {
          // Checksum mismatch
          this.onChecksumError?.()
        }
        this.reset()
        break
    }
  }
}

/**
 * Get human-readable name for a message type
 */
export function getMessageTypeName(type: number): string {
  switch (type) {
    case MSG_TICK:
      return 'TICK'
    case MSG_TRANSPORT:
      return 'TRANSPORT'
    case MSG_VOICES:
      return 'VOICES'
    case MSG_MIDI_IN:
      return 'MIDI_IN'
    case MSG_CC_STATE:
      return 'CC_STATE'
    case MSG_SYNTH_STATE:
      return 'SYNTH_STATE'
    case MSG_CC_BANK:
      return 'CC_BANK'
    case MSG_FADER_STATE:
      return 'FADER_STATE'
    case MSG_MIXER_STATE:
      return 'MIXER_STATE'
    case MSG_DEBUG:
      return 'DEBUG'
    default:
      return `0x${type.toString(16).toUpperCase()}`
  }
}
