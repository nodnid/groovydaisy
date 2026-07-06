import type { GrooveState } from '../../core/state'
import {
  GROOVE_QUANT_PADS,
  GROOVE_QUANT_KEYS,
  GROOVE_SWING,
  GROOVE_VEL_COMP,
} from '../../core/protocol'

interface GroovePanelProps {
  groove: GrooveState
  onGroove: (param: number, value: number) => void
  connected: boolean
}

const QUANT_LABELS = ['Off', 'Light', 'Hard']

function QuantSelect({
  label,
  color,
  value,
  onChange,
  disabled,
  title,
}: {
  label: string
  color: string
  value: number
  onChange: (v: number) => void
  disabled: boolean
  title: string
}) {
  return (
    <div className="flex items-center gap-1.5" title={title}>
      <span className="text-xs font-semibold" style={{ color }}>
        {label}
      </span>
      <div className="flex rounded overflow-hidden border border-groove-border">
        {QUANT_LABELS.map((name, mode) => (
          <button
            key={name}
            onClick={() => onChange(mode)}
            disabled={disabled}
            className={`px-2 py-0.5 text-xs disabled:opacity-40 ${
              value === mode
                ? 'bg-groove-accent text-white font-semibold'
                : 'bg-groove-bg text-groove-muted hover:text-groove-text'
            }`}
          >
            {name}
          </button>
        ))}
      </div>
    </div>
  )
}

/**
 * Groove settings (Phase 4). Quantize and velocity-compress act at
 * CAPTURE time (the next grab uses them); swing acts at PLAYBACK time —
 * drag it while loops run and the whole box shuffles, non-destructively.
 */
export default function GroovePanel({ groove, onGroove, connected }: GroovePanelProps) {
  return (
    <div className="bg-groove-panel border border-groove-border rounded-lg px-4 py-2">
      <div className="flex items-center gap-6 flex-wrap">
        <span className="text-xs font-bold uppercase tracking-wide text-groove-muted">
          Groove
        </span>

        <QuantSelect
          label="Quantize Pads"
          color="#3fb950"
          value={groove.quantPads}
          onChange={(v) => onGroove(GROOVE_QUANT_PADS, v)}
          disabled={!connected}
          title="Snap the NEXT pads grab to the 16th grid (Light = halfway there)"
        />
        <QuantSelect
          label="Quantize Keys"
          color="#58a6ff"
          value={groove.quantKeys}
          onChange={(v) => onGroove(GROOVE_QUANT_KEYS, v)}
          disabled={!connected}
          title="Snap the NEXT keys grab to the 16th grid (Light = halfway there)"
        />

        <label
          className="flex items-center gap-1.5 text-xs text-groove-text cursor-pointer"
          title="Lift soft pad hits on the next grab — campfire technique is wild, taming is kind"
        >
          <input
            type="checkbox"
            checked={groove.velComp}
            disabled={!connected}
            onChange={(e) => onGroove(GROOVE_VEL_COMP, e.target.checked ? 1 : 0)}
          />
          Tame pad velocity
        </label>

        <div
          className="flex items-center gap-2 ml-auto"
          title="Playback-time shuffle: 50% is straight, 66% is triplet feel, 75% is maximum. Live — tweak it while the loops run."
        >
          <span className="text-xs font-semibold text-groove-text">Swing</span>
          <input
            type="range"
            min={50}
            max={75}
            step={1}
            value={groove.swingPct}
            disabled={!connected}
            onChange={(e) => onGroove(GROOVE_SWING, parseInt(e.target.value))}
            className="w-32 accent-groove-accent"
          />
          <span
            className={`font-mono text-xs w-9 ${
              groove.swingPct > 50 ? 'text-groove-yellow' : 'text-groove-muted'
            }`}
          >
            {groove.swingPct}%
          </span>
        </div>
      </div>
    </div>
  )
}
