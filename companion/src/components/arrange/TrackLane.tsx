import { useRef, useState } from 'react'
import { TICKS_PER_BAR } from '../../core/constants'
import type { TrackState, TrackData, StripState, AudioPeaks } from '../../core/state'
import {
  KIND_MIDI_DRUM,
  KIND_MIDI_SYNTH,
  KIND_AUDIO,
  EDIT_TOGGLE_DRUM,
  EDIT_DELETE_NOTE,
  EDIT_MOVE_NOTE,
  type TrackEditArgs,
} from '../../core/protocol'

const GRID = 24 // ticks per 16th (96 PPQN)

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
  onEdit: (slot: number, gen: number, args: TrackEditArgs) => void
  onOpenEditor: (slot: number) => void
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

/** Note-content rendering: drum grid dots or piano-roll rects.
 *  With onEdit set, lanes are EDITABLE (horizon #1): click a drum cell
 *  to toggle the hit; drag a synth note (snaps to 16ths, vertical =
 *  pitch); shift-click a synth note to delete it. */
function LaneContent({
  track,
  data,
  color,
  muted,
  onEdit,
}: {
  track: TrackState
  data: TrackData | undefined
  color: string
  muted: boolean
  onEdit?: (args: TrackEditArgs) => void
}) {
  const lengthTicks = track.lengthBars * TICKS_PER_BAR
  const W = 100 // viewBox percent-ish units
  const H = 40
  const opacity = muted ? 0.25 : 0.9
  const dragRef = useRef<null | {
    note: number
    from: number
    startX: number
    startY: number
    box: DOMRect
    span: number
  }>(null)

  if (!data) {
    return (
      <text x={2} y={H / 2 + 3} fontSize="7" fill="#8b949e">
        loading…
      </text>
    )
  }
  if (data.events.length === 0 && track.kind !== KIND_MIDI_DRUM) {
    return (
      <text x={2} y={H / 2 + 3} fontSize="7" fill="#8b949e">
        (empty)
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
    // Click-to-toggle: the whole lane is a drum grid
    const handleClick = (e: React.MouseEvent<SVGRectElement>) => {
      if (!onEdit) return
      const svg = e.currentTarget.ownerSVGElement
      if (!svg) return
      const r = svg.getBoundingClientRect()
      const fx = (e.clientX - r.left) / r.width
      const fy = (e.clientY - r.top) / r.height
      const steps = track.lengthBars * 16
      const step = Math.max(0, Math.min(Math.floor(fx * steps), steps - 1))
      const row = Math.max(0, Math.min(Math.floor((1 - fy) * 8), 7))
      const note = 36 + row
      const cellStart = step * GRID
      const hit = data.events.find(
        (ev) =>
          (ev.status & 0xf0) === 0x90 &&
          ev.d2 > 0 &&
          ev.d1 === note &&
          ev.tick >= cellStart &&
          ev.tick < cellStart + GRID
      )
      onEdit({
        op: EDIT_TOGGLE_DRUM,
        tick: hit ? hit.tick : cellStart,
        note,
        vel: 100,
      })
    }
    return (
      <>
        {dots}
        <rect
          x={0}
          y={0}
          width={W}
          height={H}
          fill="transparent"
          style={onEdit ? { cursor: 'pointer' } : undefined}
          onClick={handleClick}
        />
      </>
    )
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

  const beginDrag = (e: React.PointerEvent<SVGRectElement>, note: number, from: number) => {
    if (!onEdit) return
    if (e.shiftKey) {
      onEdit({ op: EDIT_DELETE_NOTE, tick: from, note })
      return
    }
    const svg = e.currentTarget.ownerSVGElement
    if (!svg) return
    dragRef.current = {
      note,
      from,
      startX: e.clientX,
      startY: e.clientY,
      box: svg.getBoundingClientRect(),
      span,
    }
    e.currentTarget.setPointerCapture(e.pointerId)
  }

  const endDrag = (e: React.PointerEvent<SVGRectElement>) => {
    const d = dragRef.current
    dragRef.current = null
    if (!d || !onEdit) return
    const dx = e.clientX - d.startX
    const dy = e.clientY - d.startY
    if (Math.abs(dx) < 4 && Math.abs(dy) < 4) return // a click, not a drag
    const dTick = Math.round((dx / d.box.width) * lengthTicks)
    const dNote = -Math.round((dy / d.box.height) * d.span)
    let newTick = Math.round((d.from + dTick) / GRID) * GRID
    newTick = ((newTick % lengthTicks) + lengthTicks) % lengthTicks
    const newNote = Math.max(0, Math.min(127, d.note + dNote))
    if (newTick === d.from && newNote === d.note) return
    onEdit({ op: EDIT_MOVE_NOTE, tick: d.from, note: d.note, newTick, newNote })
  }

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
          <g key={`${i}-${j}`}>
            <rect
              x={(a / lengthTicks) * W}
              y={y}
              width={Math.max(((b - a) / lengthTicks) * W, 0.8)}
              height={2}
              rx={0.8}
              fill={color}
              opacity={opacity}
            />
            {onEdit && (
              <rect
                x={(a / lengthTicks) * W}
                y={y - 2}
                width={Math.max(((b - a) / lengthTicks) * W, 1.6)}
                height={6}
                fill="transparent"
                style={{ cursor: 'grab' }}
                onPointerDown={(e) => beginDrag(e, r.note, r.from)}
                onPointerUp={endDrag}
              />
            )}
          </g>
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
  onEdit,
  onOpenEditor,
  connected,
}: TrackLaneProps) {
  // Lanes are editable once the content is fully assembled (edits key on
  // exact ticks from the data we're showing)
  const editable = connected && data?.complete
  const handleEdit = editable
    ? (args: TrackEditArgs) => onEdit(track.slot, track.gen, args)
    : undefined
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
      <div className="w-44 flex-shrink-0 flex flex-col justify-center gap-1 px-2 py-1 rounded-l bg-groove-panel border border-groove-border">
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
            className="flex-1 min-w-0 h-1 accent-blue-500"
            title="Track level"
          />
          {track.kind !== KIND_AUDIO && (
            <button
              onClick={() => onOpenEditor(track.slot)}
              disabled={!connected}
              className="px-1.5 py-0.5 rounded text-[10px] bg-groove-border text-groove-muted hover:text-groove-text disabled:opacity-50"
              title="Open the editor (the lane is a window; this is the workbench)"
            >
              ✎
            </button>
          )}
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
              <LaneContent
                track={track}
                data={data}
                color={color}
                muted={muted}
                onEdit={handleEdit}
              />
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
