import { useCallback } from 'react'
import {
  SynthParams,
  SynthParamId,
  WAVEFORM_NAMES,
} from '../core/protocol'

interface SynthPanelProps {
  params: SynthParams
  onParamChange: (paramId: SynthParamId, value: number) => void
  connected: boolean
}

// Knob/slider component for continuous parameters
function ParamSlider({
  label,
  value,
  min,
  max,
  step,
  unit,
  onChange,
  disabled,
  logarithmic,
}: {
  label: string
  value: number
  min: number
  max: number
  step?: number
  unit?: string
  onChange: (value: number) => void
  disabled?: boolean
  logarithmic?: boolean
}) {
  const displayValue = logarithmic
    ? value.toFixed(value < 10 ? 2 : value < 100 ? 1 : 0)
    : value.toFixed(step && step < 1 ? 2 : 0)

  // Convert to/from logarithmic scale for display
  const toSliderValue = (v: number) => {
    if (!logarithmic) return v
    return Math.log10(v / min) / Math.log10(max / min)
  }

  const fromSliderValue = (s: number) => {
    if (!logarithmic) return s
    return min * Math.pow(max / min, s)
  }

  return (
    <div className="flex flex-col gap-1">
      <div className="flex justify-between text-xs">
        <span className="text-groove-muted">{label}</span>
        <span className="text-groove-text font-mono">
          {displayValue}
          {unit}
        </span>
      </div>
      <input
        type="range"
        min={logarithmic ? 0 : min}
        max={logarithmic ? 1 : max}
        step={logarithmic ? 0.001 : step || 0.01}
        value={toSliderValue(value)}
        onChange={(e) => onChange(fromSliderValue(parseFloat(e.target.value)))}
        disabled={disabled}
        className="w-full h-2 bg-groove-border rounded-lg appearance-none cursor-pointer
                   disabled:opacity-50 disabled:cursor-not-allowed
                   [&::-webkit-slider-thumb]:appearance-none
                   [&::-webkit-slider-thumb]:w-4
                   [&::-webkit-slider-thumb]:h-4
                   [&::-webkit-slider-thumb]:rounded-full
                   [&::-webkit-slider-thumb]:bg-groove-accent
                   [&::-webkit-slider-thumb]:cursor-pointer
                   [&::-webkit-slider-thumb]:hover:bg-blue-400"
      />
    </div>
  )
}

// Waveform selector
function WaveformSelect({
  label,
  value,
  onChange,
  disabled,
}: {
  label: string
  value: number
  onChange: (value: number) => void
  disabled?: boolean
}) {
  return (
    <div className="flex flex-col gap-1">
      <span className="text-xs text-groove-muted">{label}</span>
      <div className="flex gap-1">
        {WAVEFORM_NAMES.map((name, i) => (
          <button
            key={i}
            onClick={() => onChange(i)}
            disabled={disabled}
            className={`flex-1 px-2 py-1 text-xs rounded transition-colors
                       ${value === i
                         ? 'bg-groove-accent text-white'
                         : 'bg-groove-bg text-groove-muted hover:bg-groove-border hover:text-groove-text'
                       }
                       disabled:opacity-50 disabled:cursor-not-allowed`}
          >
            {name.substring(0, 3)}
          </button>
        ))}
      </div>
    </div>
  )
}

// Section header
function Section({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <div className="space-y-3">
      <h3 className="text-sm font-semibold text-groove-accent border-b border-groove-border pb-1">
        {title}
      </h3>
      {children}
    </div>
  )
}

export default function SynthPanel({ params, onParamChange, connected }: SynthPanelProps) {
  const handleChange = useCallback(
    (paramId: SynthParamId) => (value: number) => {
      onParamChange(paramId, value)
    },
    [onParamChange]
  )

  return (
    <div className="bg-groove-panel border border-groove-border rounded-lg">
      <div className="px-4 py-3 border-b border-groove-border">
        <h2 className="font-semibold text-groove-text">Synth Parameters</h2>
      </div>

      <div className="p-4 grid grid-cols-1 md:grid-cols-2 gap-6">
        {/* Oscillators */}
        <Section title="Oscillators">
          <div className="space-y-3">
            <WaveformSelect
              label="Osc 1 Wave"
              value={params.osc1Wave}
              onChange={handleChange(SynthParamId.OSC1_WAVE)}
              disabled={!connected}
            />
            <ParamSlider
              label="Osc 1 Level"
              value={params.osc1Level}
              min={0}
              max={1}
              onChange={handleChange(SynthParamId.OSC1_LEVEL)}
              disabled={!connected}
            />
            <WaveformSelect
              label="Osc 2 Wave"
              value={params.osc2Wave}
              onChange={handleChange(SynthParamId.OSC2_WAVE)}
              disabled={!connected}
            />
            <ParamSlider
              label="Osc 2 Level"
              value={params.osc2Level}
              min={0}
              max={1}
              onChange={handleChange(SynthParamId.OSC2_LEVEL)}
              disabled={!connected}
            />
            <ParamSlider
              label="Osc 2 Detune"
              value={params.osc2Detune}
              min={-24}
              max={24}
              step={1}
              unit=" st"
              onChange={handleChange(SynthParamId.OSC2_DETUNE)}
              disabled={!connected}
            />
          </div>
        </Section>

        {/* Filter */}
        <Section title="Filter">
          <div className="space-y-3">
            <ParamSlider
              label="Cutoff"
              value={params.filterCutoff}
              min={20}
              max={20000}
              unit=" Hz"
              logarithmic
              onChange={handleChange(SynthParamId.FILTER_CUTOFF)}
              disabled={!connected}
            />
            <ParamSlider
              label="Resonance"
              value={params.filterRes}
              min={0}
              max={1}
              onChange={handleChange(SynthParamId.FILTER_RES)}
              disabled={!connected}
            />
            <ParamSlider
              label="Env Amount"
              value={params.filterEnvAmt}
              min={0}
              max={1}
              onChange={handleChange(SynthParamId.FILTER_ENV_AMT)}
              disabled={!connected}
            />
          </div>
        </Section>

        {/* Amp Envelope */}
        <Section title="Amp Envelope">
          <div className="space-y-3">
            <ParamSlider
              label="Attack"
              value={params.ampAttack}
              min={0.001}
              max={5}
              unit=" s"
              logarithmic
              onChange={handleChange(SynthParamId.AMP_ATTACK)}
              disabled={!connected}
            />
            <ParamSlider
              label="Decay"
              value={params.ampDecay}
              min={0.001}
              max={5}
              unit=" s"
              logarithmic
              onChange={handleChange(SynthParamId.AMP_DECAY)}
              disabled={!connected}
            />
            <ParamSlider
              label="Sustain"
              value={params.ampSustain}
              min={0}
              max={1}
              onChange={handleChange(SynthParamId.AMP_SUSTAIN)}
              disabled={!connected}
            />
            <ParamSlider
              label="Release"
              value={params.ampRelease}
              min={0.001}
              max={5}
              unit=" s"
              logarithmic
              onChange={handleChange(SynthParamId.AMP_RELEASE)}
              disabled={!connected}
            />
          </div>
        </Section>

        {/* Filter Envelope */}
        <Section title="Filter Envelope">
          <div className="space-y-3">
            <ParamSlider
              label="Attack"
              value={params.filtAttack}
              min={0.001}
              max={5}
              unit=" s"
              logarithmic
              onChange={handleChange(SynthParamId.FILT_ATTACK)}
              disabled={!connected}
            />
            <ParamSlider
              label="Decay"
              value={params.filtDecay}
              min={0.001}
              max={5}
              unit=" s"
              logarithmic
              onChange={handleChange(SynthParamId.FILT_DECAY)}
              disabled={!connected}
            />
            <ParamSlider
              label="Sustain"
              value={params.filtSustain}
              min={0}
              max={1}
              onChange={handleChange(SynthParamId.FILT_SUSTAIN)}
              disabled={!connected}
            />
            <ParamSlider
              label="Release"
              value={params.filtRelease}
              min={0.001}
              max={5}
              unit=" s"
              logarithmic
              onChange={handleChange(SynthParamId.FILT_RELEASE)}
              disabled={!connected}
            />
          </div>
        </Section>

        {/* Velocity & Master */}
        <Section title="Velocity">
          <div className="space-y-3">
            <ParamSlider
              label="Vel → Amp"
              value={params.velToAmp}
              min={0}
              max={1}
              onChange={handleChange(SynthParamId.VEL_TO_AMP)}
              disabled={!connected}
            />
            <ParamSlider
              label="Vel → Filter"
              value={params.velToFilter}
              min={0}
              max={1}
              onChange={handleChange(SynthParamId.VEL_TO_FILTER)}
              disabled={!connected}
            />
          </div>
        </Section>

        <Section title="Master">
          <div className="space-y-3">
            <ParamSlider
              label="Level"
              value={params.level}
              min={0}
              max={1}
              onChange={handleChange(SynthParamId.LEVEL)}
              disabled={!connected}
            />
          </div>
        </Section>
      </div>
    </div>
  )
}
