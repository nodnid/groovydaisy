import { STRIP_GUITAR, STRIP_SYNTH, STRIP_DRUMS, STRIP_METRO, STRIP_MASTER } from '../core/constants'
import type { StripState } from '../core/state'

interface LiveMixerProps {
  strips: Record<number, StripState>
  masterOut: number // 0-127, from engine mix
  metroOn: boolean
  onStripChange: (strip: number, field: 'level' | 'pan' | 'mute', value: number) => void
  onMasterChange: (value: number) => void
  onMetroToggle: (on: boolean) => void
  connected: boolean
}

const STRIP_DEFS: Array<{ id: number; name: string }> = [
  { id: STRIP_GUITAR, name: 'Guitar' },
  { id: STRIP_SYNTH, name: 'Synth' },
  { id: STRIP_DRUMS, name: 'Drums' },
  { id: STRIP_METRO, name: 'Metro' },
]

function formatPan(v: number): string {
  const pan = v - 64
  if (pan === 0) return 'C'
  return pan < 0 ? `L${-pan}` : `R${pan}`
}

/**
 * Live-channel mixer: the fixed strips (guitar pass-through, synth engine,
 * drum engine, metronome) + master. Track strips join in Phase 2.
 */
export default function LiveMixer({
  strips,
  masterOut,
  metroOn,
  onStripChange,
  onMasterChange,
  onMetroToggle,
  connected,
}: LiveMixerProps) {
  return (
    <div className="bg-groove-panel border border-groove-border rounded-lg">
      <div className="px-4 py-3 border-b border-groove-border flex items-center justify-between">
        <h2 className="font-semibold text-groove-text">Live Mixer</h2>
        <button
          onClick={() => onMetroToggle(!metroOn)}
          disabled={!connected}
          className={`px-3 py-1 rounded text-xs font-semibold transition-colors disabled:opacity-50 ${
            metroOn
              ? 'bg-groove-accent text-white'
              : 'bg-groove-border text-groove-muted hover:text-groove-text'
          }`}
          title="Metronome on/off"
        >
          {metroOn ? 'CLICK ON' : 'CLICK OFF'}
        </button>
      </div>

      <div className="p-4 grid grid-cols-5 gap-4">
        {STRIP_DEFS.map(({ id, name }) => {
          const s = strips[id] ?? { level: 101, pan: 64, mute: false, sendRev: 0, sendDly: 0 }
          return (
            <div key={id} className="flex flex-col items-center gap-2">
              <span className="text-xs text-groove-muted font-semibold">{name}</span>

              {/* Level */}
              <input
                type="range"
                min={0}
                max={127}
                value={s.level}
                disabled={!connected}
                onChange={(e) => onStripChange(id, 'level', parseInt(e.target.value))}
                className="w-full accent-blue-500"
                title={`${name} level`}
              />
              <span className="text-xs font-mono text-groove-text">
                {Math.round((s.level / 127) * 100)}%
              </span>

              {/* Pan */}
              <input
                type="range"
                min={0}
                max={127}
                value={s.pan}
                disabled={!connected}
                onChange={(e) => onStripChange(id, 'pan', parseInt(e.target.value))}
                className="w-full accent-yellow-500"
                title={`${name} pan`}
              />
              <span className="text-xs font-mono text-groove-muted">{formatPan(s.pan)}</span>

              {/* Mute */}
              <button
                onClick={() => onStripChange(id, 'mute', s.mute ? 0 : 1)}
                disabled={!connected}
                className={`w-full py-1 rounded text-xs font-bold transition-colors disabled:opacity-50 ${
                  s.mute
                    ? 'bg-groove-red text-white'
                    : 'bg-groove-border text-groove-muted hover:text-groove-text'
                }`}
              >
                {s.mute ? 'MUTED' : 'MUTE'}
              </button>
            </div>
          )
        })}

        {/* Master */}
        <div className="flex flex-col items-center gap-2 border-l border-groove-border pl-4">
          <span className="text-xs text-groove-accent font-semibold">Master</span>
          <input
            type="range"
            min={0}
            max={127}
            value={masterOut}
            disabled={!connected}
            onChange={(e) => onMasterChange(parseInt(e.target.value))}
            className="w-full accent-green-500"
            title="Master output"
          />
          <span className="text-xs font-mono text-groove-text">
            {Math.round((masterOut / 127) * 100)}%
          </span>
        </div>
      </div>
    </div>
  )
}

export { STRIP_MASTER }
