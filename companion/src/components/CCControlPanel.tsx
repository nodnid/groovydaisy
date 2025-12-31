import { useMemo } from 'react'
import CCEncoder from './CCEncoder'
import CCFader from './CCFader'
import {
  Bank,
  BANK_NAMES,
  ENCODER_CCS,
  FADER_CCS,
  getBankMappings,
  MixerState,
  ParamTarget,
  getDefaultMixerState,
} from '../core/ccMappings'
import { SynthParams } from '../core/protocol'

export interface FaderPickupState {
  pickedUp: boolean
  needsPickup: boolean
}

export interface CCControlPanelProps {
  currentBank: Bank
  faderStates: FaderPickupState[]
  mixerState: MixerState
  synthParams: SynthParams
  onBankChange: (bank: Bank) => void
  connected: boolean
}

// Convert synth param values (floats) to CC values (0-127)
function freqToCC(freq: number): number {
  // Logarithmic: 20-20000 Hz -> 0-127
  const norm = Math.log(freq / 20) / Math.log(1000)
  return Math.round(Math.max(0, Math.min(127, norm * 127)))
}

function timeToCC(time: number): number {
  // Logarithmic: 0.001-5.0 seconds -> 0-127
  const norm = Math.log(time / 0.001) / Math.log(5000)
  return Math.round(Math.max(0, Math.min(127, norm * 127)))
}

function normToCC(value: number): number {
  // Linear: 0.0-1.0 -> 0-127
  return Math.round(Math.max(0, Math.min(127, value * 127)))
}

function waveToCC(wave: number): number {
  // Wave index 0-3 -> CC 0-127
  return Math.round((wave / 4) * 128)
}

function semitonesToCC(semitones: number): number {
  // -24 to +24 semitones -> 0-127 (64 = center)
  return Math.round(64 + (semitones * 64 / 24))
}

// Map ParamTarget to value (0-127) from mixerState or synthParams
function getValueForTarget(
  target: ParamTarget,
  mixerState: MixerState,
  synthParams: SynthParams
): number {
  switch (target) {
    // Drum levels
    case ParamTarget.DRUM_1_LEVEL: return mixerState.drumLevels[0]
    case ParamTarget.DRUM_2_LEVEL: return mixerState.drumLevels[1]
    case ParamTarget.DRUM_3_LEVEL: return mixerState.drumLevels[2]
    case ParamTarget.DRUM_4_LEVEL: return mixerState.drumLevels[3]
    case ParamTarget.DRUM_5_LEVEL: return mixerState.drumLevels[4]
    case ParamTarget.DRUM_6_LEVEL: return mixerState.drumLevels[5]
    case ParamTarget.DRUM_7_LEVEL: return mixerState.drumLevels[6]
    case ParamTarget.DRUM_8_LEVEL: return mixerState.drumLevels[7]
    // Drum pans
    case ParamTarget.DRUM_1_PAN: return mixerState.drumPans[0]
    case ParamTarget.DRUM_2_PAN: return mixerState.drumPans[1]
    case ParamTarget.DRUM_3_PAN: return mixerState.drumPans[2]
    case ParamTarget.DRUM_4_PAN: return mixerState.drumPans[3]
    case ParamTarget.DRUM_5_PAN: return mixerState.drumPans[4]
    case ParamTarget.DRUM_6_PAN: return mixerState.drumPans[5]
    case ParamTarget.DRUM_7_PAN: return mixerState.drumPans[6]
    case ParamTarget.DRUM_8_PAN: return mixerState.drumPans[7]
    // Drum master
    case ParamTarget.DRUM_MASTER_LEVEL: return mixerState.drumMaster
    // Synth mixer values
    case ParamTarget.SYNTH_LEVEL: return mixerState.synthLevel
    case ParamTarget.SYNTH_PAN: return mixerState.synthPan
    case ParamTarget.SYNTH_MASTER_LEVEL: return mixerState.synthMaster
    // Global
    case ParamTarget.MASTER_OUTPUT: return mixerState.masterOut

    // Synth parameters (convert from float to CC)
    case ParamTarget.SYNTH_OSC1_WAVE: return waveToCC(synthParams.osc1Wave)
    case ParamTarget.SYNTH_OSC2_WAVE: return waveToCC(synthParams.osc2Wave)
    case ParamTarget.SYNTH_OSC1_LEVEL: return normToCC(synthParams.osc1Level)
    case ParamTarget.SYNTH_OSC2_LEVEL: return normToCC(synthParams.osc2Level)
    case ParamTarget.SYNTH_OSC2_DETUNE: return semitonesToCC(synthParams.osc2Detune)
    case ParamTarget.SYNTH_FILTER_CUTOFF: return freqToCC(synthParams.filterCutoff)
    case ParamTarget.SYNTH_FILTER_RES: return normToCC(synthParams.filterRes)
    case ParamTarget.SYNTH_FILTER_ENV_AMT: return normToCC(synthParams.filterEnvAmt)
    case ParamTarget.SYNTH_AMP_ATTACK: return timeToCC(synthParams.ampAttack)
    case ParamTarget.SYNTH_AMP_DECAY: return timeToCC(synthParams.ampDecay)
    case ParamTarget.SYNTH_AMP_SUSTAIN: return normToCC(synthParams.ampSustain)
    case ParamTarget.SYNTH_AMP_RELEASE: return timeToCC(synthParams.ampRelease)
    case ParamTarget.SYNTH_FILT_ATTACK: return timeToCC(synthParams.filtAttack)
    case ParamTarget.SYNTH_FILT_DECAY: return timeToCC(synthParams.filtDecay)
    case ParamTarget.SYNTH_FILT_SUSTAIN: return normToCC(synthParams.filtSustain)
    case ParamTarget.SYNTH_FILT_RELEASE: return timeToCC(synthParams.filtRelease)
    case ParamTarget.SYNTH_VEL_TO_AMP: return normToCC(synthParams.velToAmp)
    case ParamTarget.SYNTH_VEL_TO_FILTER: return normToCC(synthParams.velToFilter)

    // Default for unmapped
    default: return 64
  }
}

export default function CCControlPanel({
  currentBank,
  faderStates,
  mixerState,
  synthParams,
  onBankChange,
  connected,
}: CCControlPanelProps) {
  const bankMappings = useMemo(() => getBankMappings(currentBank), [currentBank])

  // Ensure we have valid fader states (default if not provided)
  const safeFaderStates = faderStates.length === 9
    ? faderStates
    : Array(9).fill({ pickedUp: true, needsPickup: false })

  // Use default mixer state if not provided
  const safeMixerState = mixerState || getDefaultMixerState()

  return (
    <div className="bg-groove-panel border border-groove-border rounded-lg">
      {/* Header with bank tabs */}
      <div className="px-4 py-3 border-b border-groove-border flex items-center justify-between">
        <h2 className="font-semibold text-groove-text">CC Control</h2>

        {/* Bank tabs */}
        <div className="flex gap-1">
          {BANK_NAMES.map((name, idx) => (
            <button
              key={idx}
              onClick={() => onBankChange(idx as Bank)}
              disabled={!connected}
              className={`px-3 py-1 text-sm rounded transition-colors ${
                currentBank === idx
                  ? 'bg-groove-accent text-white'
                  : 'bg-groove-bg text-groove-muted hover:text-groove-text hover:bg-groove-border'
              } ${!connected ? 'opacity-50 cursor-not-allowed' : ''}`}
            >
              {name}
            </button>
          ))}
        </div>
      </div>

      {/* Content */}
      <div className="p-4 space-y-4">
        {/* Encoders row */}
        <div>
          <div className="text-xs text-groove-muted mb-2 uppercase tracking-wider">Encoders</div>
          <div className="grid grid-cols-9 gap-2">
            {bankMappings.encoders.map((mapping, idx) => (
              <CCEncoder
                key={idx}
                cc={ENCODER_CCS[idx]}
                name={mapping.name}
                value={getValueForTarget(mapping.target, safeMixerState, synthParams)}
                formatValue={mapping.formatValue}
                disabled={mapping.target === ParamTarget.NONE || !connected}
              />
            ))}
          </div>
        </div>

        {/* Faders row */}
        <div>
          <div className="text-xs text-groove-muted mb-2 uppercase tracking-wider">Faders</div>
          <div className="grid grid-cols-9 gap-2">
            {bankMappings.faders.map((mapping, idx) => (
              <CCFader
                key={idx}
                cc={FADER_CCS[idx]}
                name={mapping.name}
                value={getValueForTarget(mapping.target, safeMixerState, synthParams)}
                pickedUp={safeFaderStates[idx].pickedUp}
                needsPickup={safeFaderStates[idx].needsPickup}
                formatValue={mapping.formatValue}
                disabled={mapping.target === ParamTarget.NONE || !connected}
              />
            ))}
          </div>
        </div>

        {/* Legend */}
        <div className="flex gap-4 text-xs text-groove-muted pt-2 border-t border-groove-border">
          <div className="flex items-center gap-1">
            <div className="w-3 h-3 rounded border-2 border-groove-green" />
            <span>Tracking</span>
          </div>
          <div className="flex items-center gap-1">
            <div className="w-3 h-3 rounded border-2 border-groove-yellow" />
            <span>Needs pickup</span>
          </div>
          <div className="flex items-center gap-1">
            <div className="w-3 h-3 rounded border-2 border-groove-border" />
            <span>Inactive</span>
          </div>
        </div>
      </div>
    </div>
  )
}
