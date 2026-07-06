/**
 * GroovyDaisy Binary Protocol v2 (mirrors src/protocol.h)
 *
 * Message format: [SYNC][TYPE][LEN_LO][LEN_HI][PAYLOAD...][CHECKSUM]
 * CHECKSUM: XOR of all bytes from TYPE through PAYLOAD.
 *
 * v2 is event-driven: the firmware pushes a message when state changes;
 * nothing streams at frame rate. The playhead is interpolated client-side
 * from MSG_TRANSPORT bpm + MSG_SYNC (~5 Hz while playing).
 */

// Sync byte
export const SYNC_BYTE = 0xaa

// Message types: Daisy -> Companion
export const MSG_HELLO = 0x01
export const MSG_TRANSPORT = 0x02
export const MSG_SYNC = 0x03
export const MSG_VOICES = 0x04
export const MSG_MIDI_IN = 0x05
export const MSG_SYNTH_STATE = 0x06
export const MSG_BANK = 0x07
export const MSG_FADER_STATE = 0x08
export const MSG_MIXER = 0x09
export const MSG_METRO = 0x0a
export const MSG_ERROR = 0x0b
export const MSG_CC_STATE = 0x0c
export const MSG_ENGINE_MIX = 0x0d
export const MSG_TRACK = 0x10
export const MSG_TRACK_GONE = 0x11
export const MSG_CAPTURE = 0x12
export const MSG_TRACK_DATA = 0x13
export const MSG_SRC_ACTIVITY = 0x14
export const MSG_AUDIO_PEAKS = 0x15
export const MSG_GROOVE = 0x16
export const MSG_SCENE = 0x17
export const MSG_POOL = 0x20
export const MSG_FX = 0x21
export const MSG_METERS = 0x22
export const MSG_STATS = 0x23
export const MSG_DEBUG = 0xff

// Message types: Companion -> Daisy
export const CMD_PLAY = 0x80
export const CMD_STOP = 0x81
export const CMD_REWIND = 0x82
export const CMD_TEMPO = 0x83
export const CMD_TAP = 0x84
export const CMD_SYNTH_PARAM = 0x85
export const CMD_LOAD_PRESET = 0x86
export const CMD_SET_BANK = 0x87
export const CMD_MIXER = 0x88
export const CMD_METRO = 0x89
export const CMD_MONITOR = 0x8a
export const CMD_MIDI_INJECT = 0x8b
export const CMD_REQ_STATE = 0x90
export const CMD_CAPTURE = 0xa0
export const CMD_UNDO = 0xa1
export const CMD_TRACK_DELETE = 0xa2
export const CMD_SRC_LEN = 0xa3
export const CMD_REQ_TRACK_DATA = 0xa5
export const CMD_GROOVE = 0xa6
export const CMD_FX = 0xa7
export const CMD_TRACK_EDIT = 0xa8
export const CMD_SCENE = 0xa9

// CMD_SCENE ops
export const SCENE_SAVE = 0
export const SCENE_GO = 1
export const NUM_SCENES = 8
export const SCENE_NONE = 0xff

// CMD_TRACK_EDIT ops (mirrors protocol.h)
export const EDIT_TOGGLE_DRUM = 0
export const EDIT_DELETE_NOTE = 1
export const EDIT_MOVE_NOTE = 2

// CMD_FX param ids (mirrors protocol.h)
export const FX_REV_SIZE = 0
export const FX_REV_TONE = 1
export const FX_DLY_DIV = 2
export const FX_DLY_FB = 3

// Delay division labels (mirrors Fx::DIV_16THS)
export const DLY_DIV_NAMES = ['1/8', '1/8 dotted', '1/4', '1/4 dotted', '1/2'] as const

// CMD_GROOVE param ids (mirrors protocol.h)
export const GROOVE_QUANT_PADS = 0
export const GROOVE_QUANT_KEYS = 1
export const GROOVE_SWING = 2
export const GROOVE_VEL_COMP = 3

// Quantize modes
export const QUANT_OFF = 0
export const QUANT_LIGHT = 1
export const QUANT_HARD = 2

// Capture sources
export const SRC_PADS = 0
export const SRC_KEYS = 1
export const SRC_GUITAR = 2
export const SRC_ANY = 0xff

export const SOURCE_NAMES: Record<number, string> = {
  [SRC_PADS]: 'Pads',
  [SRC_KEYS]: 'Keys',
  [SRC_GUITAR]: 'Guitar',
  [SRC_ANY]: 'Any',
}

// MSG_CAPTURE status
export const CAP_PENDING = 0
export const CAP_COMMITTED = 1
export const CAP_REFUSED = 2

// Track kinds (mirrors track.h)
export const KIND_MIDI_DRUM = 0
export const KIND_MIDI_SYNTH = 1
export const KIND_AUDIO = 2

// MSG_ERROR codes (mirrors protocol.h)
export const ERR_TEMPO_LOCKED = 1
export const ERR_NOT_PLAYING = 2
export const ERR_POOL_FULL = 3
export const ERR_KIND_CAP = 4
export const ERR_BUSY = 5

export const ERR_NO_HISTORY = 6
export const ERR_EMPTY = 7
export const ERR_EVENTS_FULL = 8

export const ERROR_NAMES: Record<number, string> = {
  [ERR_TEMPO_LOCKED]: 'Tempo is locked (audio loops exist)',
  [ERR_NOT_PLAYING]: 'Transport is not playing',
  [ERR_POOL_FULL]: 'Audio memory is full',
  [ERR_KIND_CAP]: 'Track limit reached',
  [ERR_BUSY]: 'Capture in progress',
  [ERR_NO_HISTORY]: 'Not enough bars played yet',
  [ERR_EMPTY]: 'Nothing was played in that window',
  [ERR_EVENTS_FULL]: 'Track is full (512 events)',
}

// CMD_MIXER / MSG_MIXER field ids
export const MIX_FIELD_LEVEL = 0
export const MIX_FIELD_PAN = 1
export const MIX_FIELD_MUTE = 2
export const MIX_FIELD_SEND_REV = 3
export const MIX_FIELD_SEND_DLY = 4

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

// ---------------------------------------------------------------------------
// Parsed message types
// ---------------------------------------------------------------------------

export interface HelloMessage {
  type: typeof MSG_HELLO
  protoVer: number
  fwMajor: number
  fwMinor: number
}

export interface TransportMessage {
  type: typeof MSG_TRANSPORT
  playing: boolean
  tempoLocked: boolean
  bpm: number // float, decoded from bpm_x10
  preroll: boolean // count-in bar in progress
}

export interface SyncMessage {
  type: typeof MSG_SYNC
  tick: number
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

export interface BankMessage {
  type: typeof MSG_BANK
  bank: number // 0-3
}

export interface FaderStateMessage {
  type: typeof MSG_FADER_STATE
  states: Array<{ pickedUp: boolean; needsPickup: boolean }> // 9 faders
}

/** One mixer strip's full state (v2 strips: guitar/synth/drums/metro/tracks) */
export interface MixerStripMessage {
  type: typeof MSG_MIXER
  strip: number
  level: number // 0-127
  pan: number // 0-127, 64 = center
  mute: boolean
  sendRev: number // 0-127
  sendDly: number // 0-127
}

export interface MetroMessage {
  type: typeof MSG_METRO
  on: boolean
  level: number // 0-127
}

export interface ErrorMessage {
  type: typeof MSG_ERROR
  code: number
  context: number
}

/** Engine-internal mix dump; layout identical to v1 MSG_MIXER_STATE */
export interface EngineMixMessage {
  type: typeof MSG_ENGINE_MIX
  drumLevels: number[] // 8 values, 0-127
  drumPans: number[] // 8 values, 0-127
  drumMaster: number
  synthLevel: number
  synthPan: number
  synthMaster: number
  masterOut: number
}

export interface TrackMessage {
  type: typeof MSG_TRACK
  slot: number
  gen: number
  kind: number // KIND_MIDI_DRUM | KIND_MIDI_SYNTH | KIND_AUDIO
  lengthBars: number
  mute: boolean
  level: number
  pan: number
  sendRev: number
  sendDly: number
  createdSeq: number
}

export interface TrackGoneMessage {
  type: typeof MSG_TRACK_GONE
  slot: number
  gen: number
  reason: number // 0 = deleted, 1 = undo
}

export interface CaptureMessage {
  type: typeof MSG_CAPTURE
  status: number // CAP_PENDING | CAP_COMMITTED | CAP_REFUSED
  source: number
  bars: number
  slot: number
  gen: number
  reason: number // ERR_* when refused
}

export interface TrackEvent {
  tick: number // loop position, 0..lengthTicks-1
  status: number
  d1: number
  d2: number
}

export interface TrackDataMessage {
  type: typeof MSG_TRACK_DATA
  slot: number
  gen: number
  chunkIdx: number
  chunkCount: number
  events: TrackEvent[]
}

export interface SrcActivityMessage {
  type: typeof MSG_SRC_ACTIVITY
  padsBarsBanked: number
  padsActive: boolean
  keysBarsBanked: number
  keysActive: boolean
  guitarBarsBanked: number
  guitarActive: boolean
}

export interface AudioPeaksMessage {
  type: typeof MSG_AUDIO_PEAKS
  slot: number
  gen: number
  peaks: number[] // u8 waveform buckets, 24/bar, loop-position order
}

export interface PoolMessage {
  type: typeof MSG_POOL
  barsFree: number
  barsTotal: number // 0 = pool unlocked (no audio loops, tempo free)
}

/** Send-FX params (Phase 5) */
export interface FxMessage {
  type: typeof MSG_FX
  revSize: number // 0-127
  revTone: number // 0-127
  dlyDiv: number // index into DLY_DIV_NAMES
  dlyFb: number // 0-127
}

/** Per-strip peak meters + CPU, 10 Hz (Phase 5) */
export interface MetersMessage {
  type: typeof MSG_METERS
  masterL: number // 0-127, sqrt taper
  masterR: number
  cpuPct: number // 0-100
  strips: number[] // 36 values, indexed by strip id
}

/** Scene state (horizon #2): mute snapshots switched on the bar line */
export interface SceneMessage {
  type: typeof MSG_SCENE
  active: number // scene index or SCENE_NONE
  armed: number // scene index or SCENE_NONE (waiting for the bar line)
  definedMask: number // bit i = scene i saved
}

/** Ring-drop diagnostics (Phase 6); cumulative, all-zero = healthy */
export interface StatsMessage {
  type: typeof MSG_STATS
  midiDrops: number
  ccDrops: number
  txLappedInt: number
  txLappedExt: number
}

/** Groove settings (Phase 4): quantize at capture, swing at playback */
export interface GrooveMessage {
  type: typeof MSG_GROOVE
  quantPads: number // QUANT_OFF | QUANT_LIGHT | QUANT_HARD
  quantKeys: number
  swingPct: number // 50..75, 50 = straight
  velComp: boolean // drum-capture velocity compression
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
  | HelloMessage
  | TransportMessage
  | SyncMessage
  | VoicesMessage
  | MidiInMessage
  | DebugMessage
  | SynthStateMessage
  | BankMessage
  | FaderStateMessage
  | MixerStripMessage
  | MetroMessage
  | ErrorMessage
  | EngineMixMessage
  | TrackMessage
  | TrackGoneMessage
  | CaptureMessage
  | TrackDataMessage
  | SrcActivityMessage
  | AudioPeaksMessage
  | GrooveMessage
  | FxMessage
  | MetersMessage
  | StatsMessage
  | SceneMessage
  | PoolMessage

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

// ---------------------------------------------------------------------------
// Command builders
// ---------------------------------------------------------------------------

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

/** Tempo command; bpm may be fractional (sent as bpm*10, uint16 LE). */
export function buildTempoCommand(bpm: number): Uint8Array {
  const bpmX10 = Math.round(bpm * 10)
  return buildMessage(CMD_TEMPO, new Uint8Array([bpmX10 & 0xff, (bpmX10 >> 8) & 0xff]))
}

export function buildSynthParamCommand(paramId: SynthParamId, value: number): Uint8Array {
  const payload = new Uint8Array(5)
  payload[0] = paramId
  payload.set(writeFloat(value), 1)
  return buildMessage(CMD_SYNTH_PARAM, payload)
}

export function buildLoadPresetCommand(presetIndex: number): Uint8Array {
  return buildMessage(CMD_LOAD_PRESET, new Uint8Array([presetIndex]))
}

export function buildSetBankCommand(bank: number): Uint8Array {
  return buildMessage(CMD_SET_BANK, new Uint8Array([bank & 0x03]))
}

export function buildMixerCommand(strip: number, field: number, value: number): Uint8Array {
  return buildMessage(CMD_MIXER, new Uint8Array([strip, field, value & 0x7f]))
}

export function buildMetroCommand(on: boolean, level: number): Uint8Array {
  return buildMessage(CMD_METRO, new Uint8Array([on ? 1 : 0, level & 0x7f]))
}

export function buildMonitorCommand(on: boolean): Uint8Array {
  return buildMessage(CMD_MONITOR, new Uint8Array([on ? 1 : 0]))
}

/** bars = 0 means "use the source's preset length" */
export function buildCaptureCommand(source: number, bars = 0): Uint8Array {
  return buildMessage(CMD_CAPTURE, new Uint8Array([source, bars]))
}

export function buildUndoCommand(): Uint8Array {
  return buildMessage(CMD_UNDO)
}

export function buildTrackDeleteCommand(slot: number, gen: number): Uint8Array {
  return buildMessage(CMD_TRACK_DELETE, new Uint8Array([slot, gen]))
}

export function buildSrcLenCommand(source: number, bars: number): Uint8Array {
  return buildMessage(CMD_SRC_LEN, new Uint8Array([source, bars]))
}

export function buildReqTrackDataCommand(slot: number, gen: number): Uint8Array {
  return buildMessage(CMD_REQ_TRACK_DATA, new Uint8Array([slot, gen]))
}

/** Set one groove parameter (GROOVE_* param ids); firmware echoes MSG_GROOVE. */
export function buildGrooveCommand(param: number, value: number): Uint8Array {
  return buildMessage(CMD_GROOVE, new Uint8Array([param, value]))
}

/** Set one send-FX parameter (FX_* param ids); firmware echoes MSG_FX. */
export function buildFxCommand(param: number, value: number): Uint8Array {
  return buildMessage(CMD_FX, new Uint8Array([param, value]))
}

/** Scenes: op = SCENE_SAVE | SCENE_GO. */
export function buildSceneCommand(op: number, idx: number): Uint8Array {
  return buildMessage(CMD_SCENE, new Uint8Array([op, idx]))
}

/** Lane editing (horizon #1). Success republishes MSG_TRACK(+DATA) with a
 *  bumped gen — the UI just re-renders from the new truth. */
export interface TrackEditArgs {
  op: number // EDIT_TOGGLE_DRUM | EDIT_DELETE_NOTE | EDIT_MOVE_NOTE
  tick: number
  note: number
  vel?: number // toggle
  newTick?: number // move
  newNote?: number // move
}

export function buildTrackEditCommand(
  slot: number,
  gen: number,
  args: TrackEditArgs
): Uint8Array {
  const a = args.op === EDIT_MOVE_NOTE ? (args.newTick ?? 0) & 0xff : (args.vel ?? 100)
  const b = args.op === EDIT_MOVE_NOTE ? ((args.newTick ?? 0) >> 8) & 0xff : 0
  const c = args.op === EDIT_MOVE_NOTE ? (args.newNote ?? args.note) : 0
  return buildMessage(
    CMD_TRACK_EDIT,
    new Uint8Array([slot, gen, args.op, args.tick & 0xff, (args.tick >> 8) & 0xff, args.note, a, b, c])
  )
}

/** Play the box from the Mac: inject a MIDI event over the serial link. */
export function buildMidiInjectCommand(
  status: number,
  d1: number,
  d2: number
): Uint8Array {
  return buildMessage(CMD_MIDI_INJECT, new Uint8Array([status, d1, d2]))
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
    case MSG_HELLO:
      if (payload.length >= 3) {
        return {
          type: MSG_HELLO,
          protoVer: payload[0],
          fwMajor: payload[1],
          fwMinor: payload[2],
        }
      }
      break

    case MSG_TRANSPORT:
      if (payload.length >= 4) {
        return {
          type: MSG_TRANSPORT,
          playing: payload[0] !== 0,
          tempoLocked: payload[1] !== 0,
          bpm: (payload[2] | (payload[3] << 8)) / 10,
          preroll: payload.length >= 5 ? payload[4] !== 0 : false,
        }
      }
      break

    case MSG_SYNC:
      if (payload.length >= 4) {
        const tick =
          payload[0] | (payload[1] << 8) | (payload[2] << 16) | (payload[3] << 24)
        return { type: MSG_SYNC, tick: tick >>> 0 }
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
      // Parse synth state - must match SendSynthState() order in src/main.cpp
      // Full payload is 68 bytes; a looser check would let a truncated
      // frame read past the buffer (fix recovered from v1-history 83f9864)
      if (payload.length >= 68) {
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

    case MSG_BANK:
      if (payload.length >= 1) {
        return {
          type: MSG_BANK,
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

    case MSG_MIXER:
      if (payload.length >= 6) {
        return {
          type: MSG_MIXER,
          strip: payload[0],
          level: payload[1],
          pan: payload[2],
          mute: payload[3] !== 0,
          sendRev: payload[4],
          sendDly: payload[5],
        }
      }
      break

    case MSG_METRO:
      if (payload.length >= 2) {
        return {
          type: MSG_METRO,
          on: payload[0] !== 0,
          level: payload[1],
        }
      }
      break

    case MSG_ERROR:
      if (payload.length >= 2) {
        return {
          type: MSG_ERROR,
          code: payload[0],
          context: payload[1],
        }
      }
      break

    case MSG_TRACK:
      if (payload.length >= 11) {
        return {
          type: MSG_TRACK,
          slot: payload[0],
          gen: payload[1],
          kind: payload[2],
          lengthBars: payload[3],
          mute: payload[4] !== 0,
          level: payload[5],
          pan: payload[6],
          sendRev: payload[7],
          sendDly: payload[8],
          createdSeq: payload[9] | (payload[10] << 8),
        }
      }
      break

    case MSG_TRACK_GONE:
      if (payload.length >= 3) {
        return {
          type: MSG_TRACK_GONE,
          slot: payload[0],
          gen: payload[1],
          reason: payload[2],
        }
      }
      break

    case MSG_CAPTURE:
      if (payload.length >= 6) {
        return {
          type: MSG_CAPTURE,
          status: payload[0],
          source: payload[1],
          bars: payload[2],
          slot: payload[3],
          gen: payload[4],
          reason: payload[5],
        }
      }
      break

    case MSG_TRACK_DATA:
      if (payload.length >= 4) {
        const events: TrackEvent[] = []
        for (let i = 4; i + 5 <= payload.length; i += 5) {
          events.push({
            tick: payload[i] | (payload[i + 1] << 8),
            status: payload[i + 2],
            d1: payload[i + 3],
            d2: payload[i + 4],
          })
        }
        return {
          type: MSG_TRACK_DATA,
          slot: payload[0],
          gen: payload[1],
          chunkIdx: payload[2],
          chunkCount: payload[3],
          events,
        }
      }
      break

    case MSG_SRC_ACTIVITY:
      if (payload.length >= 4) {
        return {
          type: MSG_SRC_ACTIVITY,
          padsBarsBanked: payload[0],
          padsActive: payload[1] !== 0,
          keysBarsBanked: payload[2],
          keysActive: payload[3] !== 0,
          guitarBarsBanked: payload.length >= 6 ? payload[4] : 0,
          guitarActive: payload.length >= 6 ? payload[5] !== 0 : false,
        }
      }
      break

    case MSG_AUDIO_PEAKS:
      if (payload.length >= 4) {
        const count = payload[2] | (payload[3] << 8)
        return {
          type: MSG_AUDIO_PEAKS,
          slot: payload[0],
          gen: payload[1],
          peaks: Array.from(payload.slice(4, 4 + count)),
        }
      }
      break

    case MSG_POOL:
      if (payload.length >= 4) {
        return {
          type: MSG_POOL,
          barsFree: payload[0] | (payload[1] << 8),
          barsTotal: payload[2] | (payload[3] << 8),
        }
      }
      break

    case MSG_GROOVE:
      if (payload.length >= 4) {
        return {
          type: MSG_GROOVE,
          quantPads: payload[0],
          quantKeys: payload[1],
          swingPct: payload[2],
          velComp: payload[3] !== 0,
        }
      }
      break

    case MSG_FX:
      if (payload.length >= 4) {
        return {
          type: MSG_FX,
          revSize: payload[0],
          revTone: payload[1],
          dlyDiv: payload[2],
          dlyFb: payload[3],
        }
      }
      break

    case MSG_METERS:
      if (payload.length >= 3) {
        return {
          type: MSG_METERS,
          masterL: payload[0],
          masterR: payload[1],
          cpuPct: payload[2],
          strips: Array.from(payload.slice(3)),
        }
      }
      break

    case MSG_SCENE:
      if (payload.length >= 3) {
        return {
          type: MSG_SCENE,
          active: payload[0],
          armed: payload[1],
          definedMask: payload[2],
        }
      }
      break

    case MSG_STATS:
      if (payload.length >= 16) {
        const u32 = (o: number) =>
          (payload[o] | (payload[o + 1] << 8) | (payload[o + 2] << 16) | (payload[o + 3] << 24)) >>> 0
        return {
          type: MSG_STATS,
          midiDrops: u32(0),
          ccDrops: u32(4),
          txLappedInt: u32(8),
          txLappedExt: u32(12),
        }
      }
      break

    case MSG_ENGINE_MIX:
      // [drum_levels:8][drum_pans:8][drum_master][synth_level][synth_pan][synth_master][master_out]
      if (payload.length >= 21) {
        const drumLevels = Array.from(payload.slice(0, 8))
        const drumPans = Array.from(payload.slice(8, 16))
        return {
          type: MSG_ENGINE_MIX,
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
    case MSG_HELLO:
      return 'HELLO'
    case MSG_TRANSPORT:
      return 'TRANSPORT'
    case MSG_SYNC:
      return 'SYNC'
    case MSG_VOICES:
      return 'VOICES'
    case MSG_MIDI_IN:
      return 'MIDI_IN'
    case MSG_SYNTH_STATE:
      return 'SYNTH_STATE'
    case MSG_BANK:
      return 'BANK'
    case MSG_FADER_STATE:
      return 'FADER_STATE'
    case MSG_MIXER:
      return 'MIXER'
    case MSG_METRO:
      return 'METRO'
    case MSG_ERROR:
      return 'ERROR'
    case MSG_CC_STATE:
      return 'CC_STATE'
    case MSG_ENGINE_MIX:
      return 'ENGINE_MIX'
    case MSG_TRACK:
      return 'TRACK'
    case MSG_TRACK_GONE:
      return 'TRACK_GONE'
    case MSG_CAPTURE:
      return 'CAPTURE'
    case MSG_TRACK_DATA:
      return 'TRACK_DATA'
    case MSG_SRC_ACTIVITY:
      return 'SRC_ACTIVITY'
    case MSG_AUDIO_PEAKS:
      return 'AUDIO_PEAKS'
    case MSG_GROOVE:
      return 'GROOVE'
    case MSG_SCENE:
      return 'SCENE'
    case MSG_FX:
      return 'FX'
    case MSG_METERS:
      return 'METERS'
    case MSG_STATS:
      return 'STATS'
    case MSG_POOL:
      return 'POOL'
    case MSG_DEBUG:
      return 'DEBUG'
    default:
      return `0x${type.toString(16).toUpperCase()}`
  }
}
