import { useRef, useState } from 'react'
import { TICKS_PER_BAR } from '../../core/constants'
import type { TrackState, TrackData, StripState, AudioPeaks } from '../../core/state'
import { KIND_MIDI_DRUM, KIND_MIDI_SYNTH, KIND_AUDIO } from '../../core/protocol'

export const KIND_COLORS: Record<number, string> = {
  [KIND_MIDI_DRUM]: '#3fb950', // green
  [KIND_MIDI_SYNTH]: '#58a6ff', // blue
  2: '#d29922', // audio (Phase 3): yellow
}

export const KIND_NAMES: Record<number, string> = {
  [KIND_MIDI_DRUM]: 'Drums',
  [KIND_MIDI_SYNTH]: 'Synth',
  2: 'Audio',
}

const MAX_BARS = 8 // lane width is proportional to loop length vs this

interface TrackLaneProps {
  track: TrackState
  data: TrackData | undefined
  peaks: AudioPeaks | undefined
  strip: StripState | undefined
  nowTick: number
  playing: boolean
  onMute: (slot: number, mute: boolean) => void
  onLevel: (slot: number, level: number) => void
  onDelete: (slot: number, gen: number) => void
  connected: boolean
}

/** Waveform rendering for audio lanes: mirrored peak bars. */
function AudioContent({
  peaks,
  color,
  muted,
}: {
  peaks: AudioPeaks | undefined
  color: string
  muted: boolean
}) {
  const H = 40
  if (!peaks || peaks.peaks.length === 0) {
    return (
      <text x={2} y={H / 2 + 3} fontSize="7" fill="#8b949e">
        loading…
      </text>
    )
  }
  const n = peaks.peaks.length
  const w = 100 / n
  return (
    <>
      {peaks.peaks.map((p, i) => {
        const h = Math.max((p / 255) * (H - 4), 0.8)
        return (
          <rect
            key={i}
            x={i * w + w * 0.15}
            y={H / 2 - h / 2}
            width={w * 0.7}
            height={h}
            rx={0.6}
            fill={color}
            opacity={muted ? 0.25 : 0.9}
          />
        )
      })}
    </>
  )
}

/** Note-content rendering: drum grid dots or piano-roll rects. */
function LaneContent({
  track,
  data,
  color,
  muted,
}: {
  track: TrackState
  data: TrackData | undefined
  color: string
  muted: boolean
}) {
  const lengthTicks = track.lengthBars * TICKS_PER_BAR
  const W = 100 // viewBox percent-ish units
  const H = 40
  const opacity = muted ? 0.25 : 0.9

  if (!data || data.events.length === 0) {
    return (
      <text x={2} y={H / 2 + 3} fontSize="7" fill="#8b949e">
        {data ? '(empty)' : 'loading…'}
      </text>
    )
  }

  if (track.kind === KIND_MIDI_DRUM) {
    // 8 pad rows (notes 36-43, kick at the bottom), a dot per hit
    const dots = data.events
      .filter((e) => (e.status & 0xf0) === 0x90 && e.d2 > 0)
      .map((e, i) => {
        const row = Math.min(Math.max(e.d1 - 36, 0), 7)
        const x = (e.tick / lengthTicks) * W
        const y = H - ((row + 0.5) / 8) * H
        return <circle key={i} cx={x} cy={y} r={1.6} fill={color} opacity={opacity} />
      })
    return <>{dots}</>
  }

  // Piano roll: pair note-ons with their offs; notes crossing the loop
  // seam are drawn as two segments
  const ons = new Map<number, number>() // note -> on tick
  const rects: Array<{ note: number; from: number; to: number }> = []
  for (const e of data.events) {
    const type = e.status & 0xf0
    const isOn = type === 0x90 && e.d2 > 0
    const isOff = type === 0x80 || (type === 0x90 && e.d2 === 0)
    if (isOn) {
      ons.set(e.d1, e.tick)
    } else if (isOff && ons.has(e.d1)) {
      rects.push({ note: e.d1, from: ons.get(e.d1)!, to: e.tick })
      ons.delete(e.d1)
    }
  }
  // Unclosed notes sustain across the seam back to their on-position
  for (const [note, from] of ons) {
    rects.push({ note, from, to: from + lengthTicks - 1 })
  }

  // Captured knob motion (CC automation, Phase 4): one faint polyline per
  // CC under the notes — the recorded sweep made visible
  const ccByNum = new Map<number, Array<{ tick: number; value: number }>>()
  for (const e of data.events) {
    if ((e.status & 0xf0) === 0xb0) {
      if (!ccByNum.has(e.d1)) ccByNum.set(e.d1, [])
      ccByNum.get(e.d1)!.push({ tick: e.tick, value: e.d2 })
    }
  }
  const ccCurves = [...ccByNum.values()].map((pts, i) => (
    <polyline
      key={`cc${i}`}
      points={pts
        .map((p) => `${(p.tick / lengthTicks) * W},${H - (p.value / 127) * H}`)
        .join(' ')}
      fill="none"
      stroke={color}
      strokeWidth="0.6"
      strokeDasharray="1.5 1"
      opacity={muted ? 0.15 : 0.45}
    />
  ))

  if (rects.length === 0) return <>{ccCurves}</>
  let lo = 127
  let hi = 0
  for (const r of rects) {
    lo = Math.min(lo, r.note)
    hi = Math.max(hi, r.note)
  }
  // At least an octave of vertical span so single notes don't fill the lane
  if (hi - lo < 12) {
    const pad = Math.ceil((12 - (hi - lo)) / 2)
    lo = Math.max(0, lo - pad)
    hi = Math.min(127, hi + pad)
  }
  const span = hi - lo + 1

  return (
    <>
      {ccCurves}
      {rects.map((r, i) => {
        const y = H - ((r.note - lo + 0.5) / span) * H - 1
        const segs =
          r.to >= lengthTicks
            ? [
                [r.from, lengthTicks - 1],
                [0, r.to - lengthTicks],
              ]
            : [[r.from, r.to]]
        return segs.map(([a, b], j) => (
          <rect
            key={`${i}-${j}`}
            x={(a / lengthTicks) * W}
            y={y}
            width={Math.max(((b - a) / lengthTicks) * W, 0.8)}
            height={2}
            rx={0.8}
            fill={color}
            opacity={opacity}
          />
        ))
      })}
    </>
  )
}

export default function TrackLane({
  track,
  data,
  peaks,
  strip,
  nowTick,
  playing,
  onMute,
  onLevel,
  onDelete,
  connected,
}: TrackLaneProps) {
  const muted = strip?.mute ?? false
  const level = strip?.level ?? 101
  const color = KIND_COLORS[track.kind] ?? '#8b949e'
  const lengthTicks = track.lengthBars * TICKS_PER_BAR
  const phase = (nowTick % lengthTicks) / lengthTicks
  const widthPct = (track.lengthBars / MAX_BARS) * 100

  // Hold-to-delete (600 ms) — the gesture is the safety (SPEC)
  const [holding, setHolding] = useState(false)
  const timerRef = useRef<number | null>(null)
  const startHold = () => {
    if (!connected) return
    setHolding(true)
    timerRef.current = window.setTimeout(() => {
      setHolding(false)
      onDelete(track.slot, track.gen)
    }, 600)
  }
  const cancelHold = () => {
    setHolding(false)
    if (timerRef.current !== null) clearTimeout(timerRef.current)
  }

  return (
    <div className="flex items-stretch gap-2">
      {/* Lane header */}
      <div className="w-36 flex-shrink-0 flex flex-col justify-center gap-1 px-2 py-1 rounded-l bg-groove-panel border border-groove-border">
        <div className="flex items-center gap-1.5">
          <span className="w-2 h-2 rounded-full" style={{ background: color }} />
          <span className="text-xs font-semibold text-groove-text">
            {KIND_NAMES[track.kind] ?? '?'} {track.slot}
          </span>
          <span className="text-xs text-groove-muted ml-auto">
            {track.lengthBars} bar{track.lengthBars > 1 ? 's' : ''}
          </span>
        </div>
        <div className="flex items-center gap-1">
          <button
            onClick={() => onMute(track.slot, !muted)}
            disabled={!connected}
            className={`px-1.5 py-0.5 rounded text-[10px] font-bold disabled:opacity-50 ${
              muted
                ? 'bg-groove-red text-white'
                : 'bg-groove-border text-groove-muted hover:text-groove-text'
            }`}
          >
            M
          </button>
          <input
            type="range"
            min={0}
            max={127}
            value={level}
            disabled={!connected}
            onChange={(e) => onLevel(track.slot, parseInt(e.target.value))}
            className="flex-1 h-1 accent-blue-500"
            title="Track level"
          />
          <button
            onMouseDown={startHold}
            onMouseUp={cancelHold}
            onMouseLeave={cancelHold}
            onTouchStart={startHold}
            onTouchEnd={cancelHold}
            disabled={!connected}
            className={`px-1.5 py-0.5 rounded text-[10px] disabled:opacity-50 ${
              holding
                ? 'bg-groove-red text-white'
                : 'bg-groove-border text-groove-muted hover:text-groove-red'
            }`}
            title="Hold to delete"
          >
            ✕
          </button>
        </div>
      </div>

      {/* Lane content: width proportional to loop length (polymeter visible) */}
      <div className="flex-1 relative">
        <div
          className="relative h-full min-h-[52px] rounded-r bg-groove-bg border border-groove-border overflow-hidden"
          style={{ width: `${widthPct}%` }}
        >
          {/* bar gridlines */}
          {Array.from({ length: track.lengthBars - 1 }).map((_, i) => (
            <div
              key={i}
              className="absolute top-0 bottom-0 w-px bg-groove-border opacity-60"
              style={{ left: `${((i + 1) / track.lengthBars) * 100}%` }}
            />
          ))}
          <svg
            viewBox="0 0 100 40"
            preserveAspectRatio="none"
            className="absolute inset-0 w-full h-full"
          >
            {track.kind === KIND_AUDIO ? (
              <AudioContent peaks={peaks} color={color} muted={muted} />
            ) : (
              <LaneContent track={track} data={data} color={color} muted={muted} />
            )}
          </svg>
          {/* looping playhead */}
          {playing && !muted && (
            <div
              className="absolute top-0 bottom-0 w-0.5"
              style={{ left: `${phase * 100}%`, background: color }}
            />
          )}
        </div>
      </div>
    </div>
  )
}
