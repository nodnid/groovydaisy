import { STRIP_GUITAR, STRIP_SYNTH, STRIP_DRUMS, STRIP_METRO } from '../core/constants'
import type { DeviceState, TrackState } from '../core/state'
import { KIND_COLORS, KIND_NAMES } from './arrange/TrackLane'
import type { MixerState } from '../core/ccMappings'
import {
  FX_REV_SIZE,
  FX_REV_TONE,
  FX_DLY_DIV,
  FX_DLY_FB,
  DLY_DIV_NAMES,
} from '../core/protocol'

export type StripField = 'level' | 'pan' | 'mute' | 'sendRev' | 'sendDly'

interface MixPanelProps {
  device: DeviceState
  onStripChange: (strip: number, field: StripField, value: number) => void
  onMasterChange: (value: number) => void
  onMetroToggle: (on: boolean) => void
  onFx: (param: number, value: number) => void
  connected: boolean
}

/** Peak meter bar: sqrt-tapered u8 from MSG_METERS. */
function Meter({ value }: { value: number }) {
  const pct = Math.min((value / 127) * 100, 100)
  const hot = value > 115
  return (
    <div className="w-full h-1 rounded bg-groove-border overflow-hidden">
      <div
        className="h-full rounded transition-[width] duration-100"
        style={{
          width: `${pct}%`,
          background: hot ? '#f85149' : pct > 70 ? '#d29922' : '#3fb950',
        }}
      />
    </div>
  )
}

function formatPan(v: number): string {
  const pan = v - 64
  if (pan === 0) return 'C'
  return pan < 0 ? `L${-pan}` : `R${pan}`
}

function StripControl({
  name,
  color,
  level,
  pan,
  mute,
  sendRev,
  sendDly,
  meter,
  onChange,
  connected,
  extra,
}: {
  name: string
  color?: string
  level: number
  pan: number
  mute: boolean
  sendRev: number
  sendDly: number
  meter: number
  onChange: (field: StripField, value: number) => void
  connected: boolean
  extra?: React.ReactNode
}) {
  return (
    <div className="flex flex-col items-center gap-1.5 w-20 py-2 px-1 rounded bg-groove-bg border border-groove-border">
      <span className="text-xs font-semibold text-groove-text flex items-center gap-1">
        {color && <span className="w-2 h-2 rounded-full" style={{ background: color }} />}
        {name}
      </span>
      <Meter value={meter} />
      <input
        type="range"
        min={0}
        max={127}
        value={level}
        disabled={!connected}
        onChange={(e) => onChange('level', parseInt(e.target.value))}
        className="w-full accent-blue-500"
      />
      <span className="text-[10px] font-mono text-groove-text">
        {Math.round((level / 127) * 100)}%
      </span>
      <input
        type="range"
        min={0}
        max={127}
        value={pan}
        disabled={!connected}
        onChange={(e) => onChange('pan', parseInt(e.target.value))}
        className="w-full accent-yellow-500"
      />
      <span className="text-[10px] font-mono text-groove-muted">{formatPan(pan)}</span>
      <button
        onClick={() => onChange('mute', mute ? 0 : 1)}
        disabled={!connected}
        className={`w-full py-0.5 rounded text-[10px] font-bold disabled:opacity-50 ${
          mute
            ? 'bg-groove-red text-white'
            : 'bg-groove-border text-groove-muted hover:text-groove-text'
        }`}
      >
        {mute ? 'MUTED' : 'MUTE'}
      </button>
      <div className="w-full" title="Reverb send">
        <input
          type="range"
          min={0}
          max={127}
          value={sendRev}
          disabled={!connected}
          onChange={(e) => onChange('sendRev', parseInt(e.target.value))}
          className="w-full accent-purple-500 h-1"
        />
      </div>
      <div className="w-full -mt-1" title="Delay send">
        <input
          type="range"
          min={0}
          max={127}
          value={sendDly}
          disabled={!connected}
          onChange={(e) => onChange('sendDly', parseInt(e.target.value))}
          className="w-full accent-cyan-500 h-1"
        />
      </div>
      <span className="text-[9px] text-groove-muted -mt-1">rev / dly</span>
      {extra}
    </div>
  )
}

/**
 * THE mixer — the only place mixer state appears in the UI. Track strips,
 * the live channels (guitar/synth/drums/metro), master, and a read-only
 * view of the drum-voice internals (those are KeyLab Mix-bank territory
 * until per-voice commands exist).
 */
export default function MixPanel({
  device,
  onStripChange,
  onMasterChange,
  onMetroToggle,
  onFx,
  connected,
}: MixPanelProps) {
  const meters = device.meters
  const tracks: TrackState[] = Object.values(device.tracks).sort(
    (a, b) => a.createdSeq - b.createdSeq
  )
  const em: MixerState = device.engineMix

  const strip = (id: number) =>
    device.strips[id] ?? { level: 101, pan: 64, mute: false, sendRev: 0, sendDly: 0 }

  const LIVE: Array<{ id: number; name: string }> = [
    { id: STRIP_GUITAR, name: 'Guitar' },
    { id: STRIP_SYNTH, name: 'Synth' },
    { id: STRIP_DRUMS, name: 'Drums' },
    { id: STRIP_METRO, name: 'Click' },
  ]

  return (
    <div className="space-y-4">
      <div className="bg-groove-panel border border-groove-border rounded-lg p-4">
        <h2 className="font-semibold text-groove-text mb-3">Mixer</h2>
        <div className="flex gap-2 flex-wrap items-start">
          {/* Track strips */}
          {tracks.map((t) => {
            const s = strip(t.slot)
            return (
              <StripControl
                key={`${t.slot}-${t.gen}`}
                name={`${KIND_NAMES[t.kind] ?? '?'} ${t.slot}`}
                color={KIND_COLORS[t.kind]}
                level={s.level}
                pan={s.pan}
                mute={s.mute}
                sendRev={s.sendRev}
                sendDly={s.sendDly}
                meter={meters.strips[t.slot] ?? 0}
                connected={connected}
                onChange={(f, v) => onStripChange(t.slot, f, v)}
              />
            )
          })}

          {tracks.length > 0 && <div className="w-px self-stretch bg-groove-border mx-1" />}

          {/* Live channels */}
          {LIVE.map(({ id, name }) => {
            const s = strip(id)
            return (
              <StripControl
                key={id}
                name={name}
                level={s.level}
                pan={s.pan}
                mute={s.mute}
                sendRev={s.sendRev}
                sendDly={s.sendDly}
                meter={meters.strips[id] ?? 0}
                connected={connected}
                onChange={(f, v) => onStripChange(id, f, v)}
                extra={
                  id === STRIP_METRO ? (
                    <button
                      onClick={() => onMetroToggle(!device.metro.on)}
                      disabled={!connected}
                      className={`w-full py-0.5 rounded text-[10px] font-bold disabled:opacity-50 ${
                        device.metro.on
                          ? 'bg-groove-accent text-white'
                          : 'bg-groove-border text-groove-muted'
                      }`}
                    >
                      {device.metro.on ? 'ON' : 'OFF'}
                    </button>
                  ) : undefined
                }
              />
            )
          })}

          <div className="w-px self-stretch bg-groove-border mx-1" />

          {/* Master */}
          <div className="flex flex-col items-center gap-1.5 w-20 py-2 px-1 rounded bg-groove-bg border border-groove-accent">
            <span className="text-xs font-semibold text-groove-accent">Master</span>
            <Meter value={meters.masterL} />
            <Meter value={meters.masterR} />
            <input
              type="range"
              min={0}
              max={127}
              value={em.masterOut}
              disabled={!connected}
              onChange={(e) => onMasterChange(parseInt(e.target.value))}
              className="w-full accent-green-500"
            />
            <span className="text-[10px] font-mono text-groove-text">
              {Math.round((em.masterOut / 127) * 100)}%
            </span>
          </div>
        </div>
      </div>

      {/* Send FX (Phase 5): one shared reverb + tempo-synced ping-pong delay */}
      <div className="bg-groove-panel border border-groove-border rounded-lg p-4">
        <div className="flex items-center justify-between mb-3">
          <h2 className="font-semibold text-groove-text">
            The Space <span className="text-groove-muted text-xs font-normal">reverb + delay sends</span>
          </h2>
          <span
            className={`text-xs font-mono ${
              meters.cpuPeak > 85 ? 'text-groove-red' : 'text-groove-muted'
            }`}
            title="average / worst block — the peak is what glitches"
          >
            CPU {meters.cpuPct}% / {meters.cpuPeak}% pk
          </span>
        </div>
        <div className="flex gap-6 flex-wrap items-end">
          <label className="flex flex-col gap-1 text-xs text-groove-text">
            <span className="font-semibold text-purple-400">Reverb size</span>
            <input
              type="range"
              min={0}
              max={127}
              value={device.fx.revSize}
              disabled={!connected}
              onChange={(e) => onFx(FX_REV_SIZE, parseInt(e.target.value))}
              className="w-36 accent-purple-500"
            />
          </label>
          <label className="flex flex-col gap-1 text-xs text-groove-text">
            <span className="font-semibold text-purple-400">Reverb tone</span>
            <input
              type="range"
              min={0}
              max={127}
              value={device.fx.revTone}
              disabled={!connected}
              onChange={(e) => onFx(FX_REV_TONE, parseInt(e.target.value))}
              className="w-36 accent-purple-500"
            />
          </label>
          <label className="flex flex-col gap-1 text-xs text-groove-text">
            <span className="font-semibold text-cyan-400">Delay time</span>
            <select
              value={device.fx.dlyDiv}
              disabled={!connected}
              onChange={(e) => onFx(FX_DLY_DIV, parseInt(e.target.value))}
              className="bg-groove-bg border border-groove-border rounded text-groove-text px-2 py-1"
            >
              {DLY_DIV_NAMES.map((name, i) => (
                <option key={name} value={i}>
                  {name}
                </option>
              ))}
            </select>
          </label>
          <label className="flex flex-col gap-1 text-xs text-groove-text">
            <span className="font-semibold text-cyan-400">Delay feedback</span>
            <input
              type="range"
              min={0}
              max={127}
              value={device.fx.dlyFb}
              disabled={!connected}
              onChange={(e) => onFx(FX_DLY_FB, parseInt(e.target.value))}
              className="w-36 accent-cyan-500"
            />
          </label>
          <p className="text-[11px] text-groove-muted max-w-52">
            Per-strip sends are the small purple/cyan sliders on each mixer
            strip. Delay follows the tempo.
          </p>
        </div>
      </div>

      {/* Drum voice internals (read-only mirror; control via KeyLab Mix bank) */}
      <div className="bg-groove-panel border border-groove-border rounded-lg p-4">
        <h2 className="font-semibold text-groove-text mb-1">Drum Voices</h2>
        <p className="text-xs text-groove-muted mb-3">
          Levels/pans inside the drum engine — adjust from the KeyLab Mix bank.
        </p>
        <div className="grid grid-cols-8 gap-2 text-center">
          {['Kick', 'Snare', 'HH-C', 'HH-O', 'Clap', 'TomL', 'TomM', 'Rim'].map(
            (name, i) => (
              <div key={name} className="rounded bg-groove-bg border border-groove-border py-2">
                <div className="text-[10px] text-groove-muted">{name}</div>
                <div className="text-xs font-mono text-groove-text">
                  {Math.round(((em.drumLevels[i] ?? 0) / 127) * 100)}%
                </div>
                <div className="text-[10px] font-mono text-groove-muted">
                  {formatPan(em.drumPans[i] ?? 64)}
                </div>
              </div>
            )
          )}
        </div>
      </div>
    </div>
  )
}
