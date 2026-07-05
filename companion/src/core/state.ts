/**
 * Device state reducer — the portable heart of the companion app.
 *
 * Pure function of (state, message): no React, no DOM, no WebSerial —
 * this file must stay portable to a future phone app / hardware screen
 * (SPEC.md, companion responsibilities). App.tsx wraps it in useReducer.
 *
 * Scrolling logs (MIDI monitor, raw log, debug) intentionally live
 * OUTSIDE this state: they're unbounded UI artifacts, not device state.
 */

import {
  MSG_HELLO,
  MSG_TRANSPORT,
  MSG_SYNC,
  MSG_VOICES,
  MSG_SYNTH_STATE,
  MSG_BANK,
  MSG_FADER_STATE,
  MSG_MIXER,
  MSG_METRO,
  MSG_ERROR,
  MSG_ENGINE_MIX,
  MSG_TRACK,
  MSG_TRACK_GONE,
  MSG_CAPTURE,
  MSG_TRACK_DATA,
  MSG_SRC_ACTIVITY,
  type ParsedMessage,
  type SynthParams,
  type TrackEvent,
  getDefaultSynthParams,
} from './protocol'
import { type MixerState, getDefaultMixerState } from './ccMappings'
import { STRIP_GUITAR, STRIP_SYNTH, STRIP_DRUMS, STRIP_METRO } from './constants'

export interface StripState {
  level: number // 0-127
  pan: number // 0-127, 64 = center
  mute: boolean
  sendRev: number
  sendDly: number
}

export interface TransportState {
  playing: boolean
  tempoLocked: boolean
  bpm: number
  preroll: boolean // count-in bar in progress
}

/** Reference point for client-side playhead interpolation. */
export interface SyncRef {
  tick: number
  atMs: number // performance/wall time when the sync message arrived
}

export interface DeviceError {
  code: number
  context: number
  atMs: number
}

export interface TrackState {
  slot: number
  gen: number
  kind: number // KIND_MIDI_DRUM | KIND_MIDI_SYNTH | KIND_AUDIO
  lengthBars: number
  createdSeq: number
}

export interface CaptureFlash {
  status: number // CAP_*
  source: number
  bars: number
  slot: number
  reason: number
  atMs: number
}

/** Assembled (possibly still partial) note content of a track.
 *  Chunks are keyed by index so retries/duplicates can't double events. */
export interface TrackData {
  gen: number
  chunkCount: number
  chunks: Record<number, TrackEvent[]>
  events: TrackEvent[] // derived: chunks flattened in index order
  complete: boolean
  lastChunkAtMs: number // for stall detection -> re-request
}

/** Rolling-buffer visibility per capture source. */
export interface SrcActivity {
  padsBarsBanked: number
  padsActive: boolean
  keysBarsBanked: number
  keysActive: boolean
}

export interface DeviceState {
  protoVer: number | null // null until MSG_HELLO seen
  transport: TransportState
  sync: SyncRef
  voices: { synth: number; drums: number }
  synthParams: SynthParams
  presetIndex: number
  bank: number
  faderStates: Array<{ pickedUp: boolean; needsPickup: boolean }>
  engineMix: MixerState
  strips: Record<number, StripState>
  metro: { on: boolean; level: number }
  lastError: DeviceError | null
  tracks: Record<number, TrackState> // keyed by slot
  trackData: Record<number, TrackData> // keyed by slot
  captureFlash: CaptureFlash | null
  srcActivity: SrcActivity
}

function defaultStrip(level = 101): StripState {
  return { level, pan: 64, mute: false, sendRev: 0, sendDly: 0 }
}

export function getInitialDeviceState(): DeviceState {
  return {
    protoVer: null,
    transport: { playing: false, tempoLocked: false, bpm: 120, preroll: false },
    sync: { tick: 0, atMs: 0 },
    voices: { synth: 0, drums: 0 },
    synthParams: getDefaultSynthParams(),
    presetIndex: 0,
    bank: 2, // Synth bank (firmware default)
    faderStates: Array(9).fill({ pickedUp: true, needsPickup: false }),
    engineMix: getDefaultMixerState(),
    strips: {
      [STRIP_GUITAR]: defaultStrip(),
      [STRIP_SYNTH]: defaultStrip(),
      [STRIP_DRUMS]: defaultStrip(),
      [STRIP_METRO]: defaultStrip(64),
    },
    metro: { on: true, level: 64 },
    lastError: null,
    tracks: {},
    trackData: {},
    captureFlash: null,
    srcActivity: {
      padsBarsBanked: 0,
      padsActive: false,
      keysBarsBanked: 0,
      keysActive: false,
    },
  }
}

export type DeviceAction =
  | { kind: 'message'; msg: ParsedMessage; nowMs: number }
  | { kind: 'reset' }
  | { kind: 'clearError' }
  | { kind: 'clearCaptureFlash' }

export function deviceReducer(state: DeviceState, action: DeviceAction): DeviceState {
  if (action.kind === 'reset') {
    return getInitialDeviceState()
  }
  if (action.kind === 'clearError') {
    return { ...state, lastError: null }
  }
  if (action.kind === 'clearCaptureFlash') {
    return { ...state, captureFlash: null }
  }

  const { msg, nowMs } = action
  switch (msg.type) {
    case MSG_HELLO:
      return { ...state, protoVer: msg.protoVer }

    case MSG_TRANSPORT: {
      const transport = {
        playing: msg.playing,
        tempoLocked: msg.tempoLocked,
        bpm: msg.bpm,
        preroll: msg.preroll,
      }
      // A transport change is also a fresh interpolation anchor: on stop
      // the playhead freezes at the current sync; on play it starts there.
      return { ...state, transport, sync: { ...state.sync, atMs: nowMs } }
    }

    case MSG_SYNC:
      return { ...state, sync: { tick: msg.tick, atMs: nowMs } }

    case MSG_VOICES:
      return { ...state, voices: { synth: msg.synth, drums: msg.drums } }

    case MSG_SYNTH_STATE:
      return { ...state, synthParams: msg.params, presetIndex: msg.presetIndex }

    case MSG_BANK:
      return { ...state, bank: msg.bank }

    case MSG_FADER_STATE:
      return { ...state, faderStates: msg.states }

    case MSG_MIXER:
      return {
        ...state,
        strips: {
          ...state.strips,
          [msg.strip]: {
            level: msg.level,
            pan: msg.pan,
            mute: msg.mute,
            sendRev: msg.sendRev,
            sendDly: msg.sendDly,
          },
        },
      }

    case MSG_METRO:
      return { ...state, metro: { on: msg.on, level: msg.level } }

    case MSG_ERROR:
      return {
        ...state,
        lastError: { code: msg.code, context: msg.context, atMs: nowMs },
      }

    case MSG_TRACK: {
      // Upsert the track and seed its mixer strip from the same message
      const track: TrackState = {
        slot: msg.slot,
        gen: msg.gen,
        kind: msg.kind,
        lengthBars: msg.lengthBars,
        createdSeq: msg.createdSeq,
      }
      return {
        ...state,
        tracks: { ...state.tracks, [msg.slot]: track },
        strips: {
          ...state.strips,
          [msg.slot]: {
            level: msg.level,
            pan: msg.pan,
            mute: msg.mute,
            sendRev: msg.sendRev,
            sendDly: msg.sendDly,
          },
        },
      }
    }

    case MSG_TRACK_GONE: {
      const existing = state.tracks[msg.slot]
      if (!existing || existing.gen !== msg.gen) {
        return state // stale reference: a different generation lives there
      }
      const tracks = { ...state.tracks }
      delete tracks[msg.slot]
      const trackData = { ...state.trackData }
      delete trackData[msg.slot]
      return { ...state, tracks, trackData }
    }

    case MSG_TRACK_DATA: {
      const prev = state.trackData[msg.slot]
      // A different generation (or chunk plan) starts a fresh assembly
      const chunks: Record<number, TrackEvent[]> =
        prev && prev.gen === msg.gen && prev.chunkCount === msg.chunkCount
          ? { ...prev.chunks, [msg.chunkIdx]: msg.events }
          : { [msg.chunkIdx]: msg.events }
      const seen = Object.keys(chunks).length
      const complete = seen >= msg.chunkCount
      const events: TrackEvent[] = []
      for (let i = 0; i < msg.chunkCount; i++) {
        if (chunks[i]) events.push(...chunks[i])
      }
      return {
        ...state,
        trackData: {
          ...state.trackData,
          [msg.slot]: {
            gen: msg.gen,
            chunkCount: msg.chunkCount,
            chunks,
            events,
            complete,
            lastChunkAtMs: nowMs,
          },
        },
      }
    }

    case MSG_SRC_ACTIVITY:
      return {
        ...state,
        srcActivity: {
          padsBarsBanked: msg.padsBarsBanked,
          padsActive: msg.padsActive,
          keysBarsBanked: msg.keysBarsBanked,
          keysActive: msg.keysActive,
        },
      }

    case MSG_CAPTURE:
      return {
        ...state,
        captureFlash: {
          status: msg.status,
          source: msg.source,
          bars: msg.bars,
          slot: msg.slot,
          reason: msg.reason,
          atMs: nowMs,
        },
      }

    case MSG_ENGINE_MIX:
      return {
        ...state,
        engineMix: {
          drumLevels: msg.drumLevels,
          drumPans: msg.drumPans,
          drumMaster: msg.drumMaster,
          synthLevel: msg.synthLevel,
          synthPan: msg.synthPan,
          synthMaster: msg.synthMaster,
          masterOut: msg.masterOut,
        },
      }

    default:
      return state
  }
}

/**
 * Interpolate the playhead tick for display.
 * tick advances at bpm * PPQN / 60 per second from the last sync anchor.
 */
export function interpolateTick(
  sync: SyncRef,
  transport: TransportState,
  nowMs: number,
  ppqn: number
): number {
  if (!transport.playing) {
    return sync.tick
  }
  const elapsedS = Math.max(0, (nowMs - sync.atMs) / 1000)
  return sync.tick + Math.floor(elapsedS * ((transport.bpm * ppqn) / 60))
}
