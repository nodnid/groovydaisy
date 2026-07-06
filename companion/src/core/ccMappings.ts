/**
 * CC Mappings for KeyLab Essential 61
 *
 * Mirrors the 4-bank CC mapping system from cc_map.h
 */

// Bank definitions
export enum Bank {
  GENERAL = 0,
  MIX = 1,
  SYNTH = 2,
  SAMPLER = 3,
}

export const BANK_NAMES = ['Live', 'Mix', 'Synth', 'Drums+'] as const

export const NUM_ENCODERS = 9
export const NUM_FADERS = 9

// Hardware CC numbers (in physical order L->R)
export const ENCODER_CCS = [74, 71, 76, 77, 93, 18, 19, 16, 17] as const
export const FADER_CCS = [73, 75, 79, 72, 80, 81, 82, 83, 85] as const

// Parameter target types (matches cc_map.h ParamTarget enum)
export enum ParamTarget {
  NONE = 0,
  // Synth params
  SYNTH_OSC1_WAVE,
  SYNTH_OSC2_WAVE,
  SYNTH_OSC1_LEVEL,
  SYNTH_OSC2_LEVEL,
  SYNTH_OSC2_DETUNE,
  SYNTH_FILTER_CUTOFF,
  SYNTH_FILTER_RES,
  SYNTH_FILTER_ENV_AMT,
  SYNTH_AMP_ATTACK,
  SYNTH_AMP_DECAY,
  SYNTH_AMP_SUSTAIN,
  SYNTH_AMP_RELEASE,
  SYNTH_FILT_ATTACK,
  SYNTH_FILT_DECAY,
  SYNTH_FILT_SUSTAIN,
  SYNTH_FILT_RELEASE,
  SYNTH_VEL_TO_AMP,
  SYNTH_VEL_TO_FILTER,
  SYNTH_LEVEL,
  SYNTH_PAN,
  SYNTH_MASTER_LEVEL,
  // Drum params
  DRUM_1_LEVEL,
  DRUM_2_LEVEL,
  DRUM_3_LEVEL,
  DRUM_4_LEVEL,
  DRUM_5_LEVEL,
  DRUM_6_LEVEL,
  DRUM_7_LEVEL,
  DRUM_8_LEVEL,
  DRUM_1_PAN,
  DRUM_2_PAN,
  DRUM_3_PAN,
  DRUM_4_PAN,
  DRUM_5_PAN,
  DRUM_6_PAN,
  DRUM_7_PAN,
  DRUM_8_PAN,
  DRUM_MASTER_LEVEL,
  // Global
  MASTER_OUTPUT,
  // v2 live-channel params (mixer strips; matches cc_map.h)
  GUITAR_LEVEL,
  GUITAR_PAN,
  METRO_LEVEL,
  // v2 capture lengths + groove
  CAPTURE_LEN_PADS,
  CAPTURE_LEN_KEYS,
  SWING,
  // v2 send FX (Phase 5)
  FX_REV_SIZE,
  FX_REV_TONE,
  FX_DLY_DIV,
  FX_DLY_FB,
  GTR_SEND_REV,
  GTR_SEND_DLY,
  SYNTH_SEND_REV,
  DRUMS_SEND_REV,
  // v2 drum sound design (Phase 5)
  DRUM_1_PITCH, DRUM_2_PITCH, DRUM_3_PITCH, DRUM_4_PITCH,
  DRUM_5_PITCH, DRUM_6_PITCH, DRUM_7_PITCH, DRUM_8_PITCH,
  DRUM_1_DECAY, DRUM_2_DECAY, DRUM_3_DECAY, DRUM_4_DECAY,
  DRUM_5_DECAY, DRUM_6_DECAY, DRUM_7_DECAY, DRUM_8_DECAY,
}

// Control mapping entry
export interface ControlMapping {
  target: ParamTarget
  name: string
  unit?: string
  formatValue?: (value: number) => string
}

// Bank mappings structure
export interface BankMappings {
  bankName: string
  encoders: ControlMapping[]
  faders: ControlMapping[]
}

// Value formatters
const formatPercent = (v: number) => `${Math.round((v / 127) * 100)}%`
const formatPan = (v: number) => {
  const pan = v - 64
  if (pan === 0) return 'C'
  return pan < 0 ? `L${-pan}` : `R${pan}`
}
const formatFreq = (v: number) => {
  const norm = v / 127
  const freq = 20 * Math.pow(1000, norm)
  return freq >= 1000 ? `${(freq / 1000).toFixed(1)}k` : `${Math.round(freq)}`
}
const formatTime = (v: number) => {
  const norm = v / 127
  const time = 0.001 * Math.pow(5000, norm)
  return time >= 1 ? `${time.toFixed(1)}s` : `${Math.round(time * 1000)}ms`
}
const formatWave = (v: number) => {
  const waves = ['Sin', 'Tri', 'Saw', 'Sqr']
  const idx = Math.floor((v * 4) / 128)
  return waves[Math.min(idx, 3)]
}
const formatSemi = (v: number) => {
  const semi = Math.round(((v - 64) * 24) / 64)
  return semi >= 0 ? `+${semi}` : `${semi}`
}
const formatDiv = (v: number) => {
  const divs = ['1/8', '1/8.', '1/4', '1/4.', '1/2']
  return divs[Math.min(Math.floor((v * 5) / 128), 4)]
}
const formatSwing = (v: number) => `${50 + Math.floor((v * 25) / 127)}%`
const formatBars = (v: number) => `${1 << Math.floor(v / 32)} bar`
const formatDrumPitch = (v: number) => {
  const semi = Math.round(((v - 64) * 12) / 64)
  return semi >= 0 ? `+${semi}st` : `${semi}st`
}
const formatDecayTime = (v: number) => {
  const t = 0.03 * Math.pow(100, v / 127)
  return t >= 1 ? `${t.toFixed(1)}s` : `${Math.round(t * 1000)}ms`
}

// Bank 0: Live (masters, space, groove — Phase 5 "Bank 4 finalized")
const BANK_GENERAL_MAPPINGS: BankMappings = {
  bankName: 'Live',
  encoders: [
    { target: ParamTarget.GUITAR_PAN, name: 'Gtr Pan', formatValue: formatPan },
    { target: ParamTarget.FX_REV_SIZE, name: 'Rev Size', formatValue: formatPercent },
    { target: ParamTarget.FX_REV_TONE, name: 'Rev Tone', formatValue: formatPercent },
    { target: ParamTarget.FX_DLY_DIV, name: 'Dly Div', formatValue: formatDiv },
    { target: ParamTarget.FX_DLY_FB, name: 'Dly FB', formatValue: formatPercent },
    { target: ParamTarget.SWING, name: 'Swing', formatValue: formatSwing },
    { target: ParamTarget.CAPTURE_LEN_PADS, name: 'Pad Len', formatValue: formatBars },
    { target: ParamTarget.CAPTURE_LEN_KEYS, name: 'Key Len', formatValue: formatBars },
    { target: ParamTarget.NONE, name: '---' },
  ],
  faders: [
    { target: ParamTarget.DRUM_MASTER_LEVEL, name: 'Drum Mst', formatValue: formatPercent },
    { target: ParamTarget.SYNTH_MASTER_LEVEL, name: 'Synth Mst', formatValue: formatPercent },
    { target: ParamTarget.GUITAR_LEVEL, name: 'Guitar', formatValue: formatPercent },
    { target: ParamTarget.METRO_LEVEL, name: 'Metro', formatValue: formatPercent },
    { target: ParamTarget.GTR_SEND_REV, name: 'Gtr Rev', formatValue: formatPercent },
    { target: ParamTarget.GTR_SEND_DLY, name: 'Gtr Dly', formatValue: formatPercent },
    { target: ParamTarget.SYNTH_SEND_REV, name: 'Syn Rev', formatValue: formatPercent },
    { target: ParamTarget.DRUMS_SEND_REV, name: 'Drm Rev', formatValue: formatPercent },
    { target: ParamTarget.MASTER_OUTPUT, name: 'Master', formatValue: formatPercent },
  ],
}

// Bank 1: Mix (Individual Levels + Pan)
const BANK_MIX_MAPPINGS: BankMappings = {
  bankName: 'Mix',
  encoders: [
    { target: ParamTarget.DRUM_1_PAN, name: 'Kick Pan', formatValue: formatPan },
    { target: ParamTarget.DRUM_2_PAN, name: 'Snare Pan', formatValue: formatPan },
    { target: ParamTarget.DRUM_3_PAN, name: 'HH-C Pan', formatValue: formatPan },
    { target: ParamTarget.DRUM_4_PAN, name: 'HH-O Pan', formatValue: formatPan },
    { target: ParamTarget.DRUM_5_PAN, name: 'Clap Pan', formatValue: formatPan },
    { target: ParamTarget.DRUM_6_PAN, name: 'TomL Pan', formatValue: formatPan },
    { target: ParamTarget.DRUM_7_PAN, name: 'TomM Pan', formatValue: formatPan },
    { target: ParamTarget.DRUM_8_PAN, name: 'Rim Pan', formatValue: formatPan },
    { target: ParamTarget.SYNTH_PAN, name: 'Synth Pan', formatValue: formatPan },
  ],
  faders: [
    { target: ParamTarget.DRUM_1_LEVEL, name: 'Kick', formatValue: formatPercent },
    { target: ParamTarget.DRUM_2_LEVEL, name: 'Snare', formatValue: formatPercent },
    { target: ParamTarget.DRUM_3_LEVEL, name: 'HH-C', formatValue: formatPercent },
    { target: ParamTarget.DRUM_4_LEVEL, name: 'HH-O', formatValue: formatPercent },
    { target: ParamTarget.DRUM_5_LEVEL, name: 'Clap', formatValue: formatPercent },
    { target: ParamTarget.DRUM_6_LEVEL, name: 'Tom L', formatValue: formatPercent },
    { target: ParamTarget.DRUM_7_LEVEL, name: 'Tom M', formatValue: formatPercent },
    { target: ParamTarget.DRUM_8_LEVEL, name: 'Rim', formatValue: formatPercent },
    { target: ParamTarget.SYNTH_LEVEL, name: 'Synth', formatValue: formatPercent },
  ],
}

// Bank 2: Synth (Sound Design)
const BANK_SYNTH_MAPPINGS: BankMappings = {
  bankName: 'Synth',
  encoders: [
    { target: ParamTarget.SYNTH_FILTER_CUTOFF, name: 'Cutoff', formatValue: formatFreq, unit: 'Hz' },
    { target: ParamTarget.SYNTH_FILT_ATTACK, name: 'Flt Atk', formatValue: formatTime },
    { target: ParamTarget.SYNTH_FILT_DECAY, name: 'Flt Dcy', formatValue: formatTime },
    { target: ParamTarget.SYNTH_OSC2_DETUNE, name: 'Detune', formatValue: formatSemi, unit: 'st' },
    { target: ParamTarget.SYNTH_AMP_ATTACK, name: 'Amp Atk', formatValue: formatTime },
    { target: ParamTarget.SYNTH_AMP_DECAY, name: 'Amp Dcy', formatValue: formatTime },
    { target: ParamTarget.SYNTH_AMP_RELEASE, name: 'Amp Rel', formatValue: formatTime },
    { target: ParamTarget.SYNTH_OSC1_WAVE, name: 'Wave 1', formatValue: formatWave },
    { target: ParamTarget.SYNTH_OSC2_WAVE, name: 'Wave 2', formatValue: formatWave },
  ],
  faders: [
    { target: ParamTarget.SYNTH_OSC1_LEVEL, name: 'Osc 1', formatValue: formatPercent },
    { target: ParamTarget.SYNTH_OSC2_LEVEL, name: 'Osc 2', formatValue: formatPercent },
    { target: ParamTarget.SYNTH_FILTER_RES, name: 'Reso', formatValue: formatPercent },
    { target: ParamTarget.SYNTH_FILTER_ENV_AMT, name: 'Flt Env', formatValue: formatPercent },
    { target: ParamTarget.SYNTH_AMP_SUSTAIN, name: 'Amp Sus', formatValue: formatPercent },
    { target: ParamTarget.SYNTH_FILT_SUSTAIN, name: 'Flt Sus', formatValue: formatPercent },
    { target: ParamTarget.SYNTH_FILT_RELEASE, name: 'Flt Rel', formatValue: formatTime },
    { target: ParamTarget.NONE, name: '---' },
    { target: ParamTarget.SYNTH_LEVEL, name: 'Level', formatValue: formatPercent },
  ],
}

// Bank 3: Drums+ (per-voice sound design, Phase 5)
const BANK_SAMPLER_MAPPINGS: BankMappings = {
  bankName: 'Drums+',
  encoders: [
    { target: ParamTarget.DRUM_1_PITCH, name: 'Kick Pit', formatValue: formatDrumPitch },
    { target: ParamTarget.DRUM_2_PITCH, name: 'Snr Pit', formatValue: formatDrumPitch },
    { target: ParamTarget.DRUM_3_PITCH, name: 'HHC Pit', formatValue: formatDrumPitch },
    { target: ParamTarget.DRUM_4_PITCH, name: 'HHO Pit', formatValue: formatDrumPitch },
    { target: ParamTarget.DRUM_5_PITCH, name: 'Clap Pit', formatValue: formatDrumPitch },
    { target: ParamTarget.DRUM_6_PITCH, name: 'TomL Pit', formatValue: formatDrumPitch },
    { target: ParamTarget.DRUM_7_PITCH, name: 'TomM Pit', formatValue: formatDrumPitch },
    { target: ParamTarget.DRUM_8_PITCH, name: 'Rim Pit', formatValue: formatDrumPitch },
    { target: ParamTarget.NONE, name: '---' },
  ],
  faders: [
    { target: ParamTarget.DRUM_1_DECAY, name: 'Kick Dcy', formatValue: formatDecayTime },
    { target: ParamTarget.DRUM_2_DECAY, name: 'Snr Dcy', formatValue: formatDecayTime },
    { target: ParamTarget.DRUM_3_DECAY, name: 'HHC Dcy', formatValue: formatDecayTime },
    { target: ParamTarget.DRUM_4_DECAY, name: 'HHO Dcy', formatValue: formatDecayTime },
    { target: ParamTarget.DRUM_5_DECAY, name: 'Clap Dcy', formatValue: formatDecayTime },
    { target: ParamTarget.DRUM_6_DECAY, name: 'TomL Dcy', formatValue: formatDecayTime },
    { target: ParamTarget.DRUM_7_DECAY, name: 'TomM Dcy', formatValue: formatDecayTime },
    { target: ParamTarget.DRUM_8_DECAY, name: 'Rim Dcy', formatValue: formatDecayTime },
    { target: ParamTarget.DRUM_MASTER_LEVEL, name: 'Drum Mst', formatValue: formatPercent },
  ],
}

// All bank mappings
export const ALL_BANKS: BankMappings[] = [
  BANK_GENERAL_MAPPINGS,
  BANK_MIX_MAPPINGS,
  BANK_SYNTH_MAPPINGS,
  BANK_SAMPLER_MAPPINGS,
]

// Get bank mappings by bank index
export function getBankMappings(bank: Bank): BankMappings {
  return ALL_BANKS[bank] ?? ALL_BANKS[0]
}

// Fader pickup state
export interface FaderState {
  pickedUp: boolean
  needsPickup: boolean
}

// Mixer state from Daisy
export interface MixerState {
  drumLevels: number[]  // 8 values, 0-127
  drumPans: number[]    // 8 values, 0=left, 64=center, 127=right
  drumMaster: number    // 0-127
  synthLevel: number    // 0-127
  synthPan: number      // 0-127
  synthMaster: number   // 0-127
  masterOut: number     // 0-127
}

// Default mixer state
export function getDefaultMixerState(): MixerState {
  return {
    drumLevels: Array(8).fill(100),
    drumPans: Array(8).fill(64),
    drumMaster: 100,
    synthLevel: 89,  // 0.7 * 127
    synthPan: 64,
    synthMaster: 127,
    masterOut: 127,
  }
}
