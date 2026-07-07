import { useRef } from 'react'
import { TICKS_PER_BAR } from '../../core/constants'
import type { TrackState, TrackData } from '../../core/state'
import {
  KIND_MIDI_DRUM,
  EDIT_TOGGLE_DRUM,
  EDIT_DELETE_NOTE,
  EDIT_MOVE_NOTE,
  EDIT_ADD_NOTE,
  type TrackEditArgs,
} from '../../core/protocol'
import { KIND_COLORS, KIND_NAMES } from './TrackLane'

const GRID = 24 // ticks per 16th

const DRUM_NAMES = ['Kick', 'Snare', 'HH-C', 'HH-O', 'Clap', 'Tom L', 'Tom M', 'Rim']
const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']

function noteName(n: number): string {
  return `${NOTE_NAMES[n % 12]}${Math.floor(n / 12) - 1}`
}

interface TrackEditorProps {
  track: TrackState
  data: TrackData | undefined
  onEdit: (slot: number, gen: number, args: TrackEditArgs) => void
  onClose: () => void
  connected: boolean
}

/**
 * The workbench (feel notes 2026-07-06): the lane is a window, this is
 * where you actually edit. Drum tracks open as a labeled step grid;
 * synth tracks open as a full-height piano roll with note names.
 *
 * Drums:  click a cell to toggle the hit.
 * Synth:  click an empty cell to ADD a note (2 sixteenths),
 *         drag a note to MOVE it (snaps to 16ths + semitones),
 *         shift-click a note to DELETE it.
 * Every edit round-trips through the firmware (CMD_TRACK_EDIT) and the
 * grid re-renders from the republished truth.
 */
export default function TrackEditor({
  track,
  data,
  onEdit,
  onClose,
  connected,
}: TrackEditorProps) {
  const lengthTicks = track.lengthBars * TICKS_PER_BAR
  const steps = track.lengthBars * 16
  const color = KIND_COLORS[track.kind] ?? '#8b949e'
  const editable = connected && data?.complete
  const events = data?.events ?? []
  const edit = (args: TrackEditArgs) => {
    if (editable) onEdit(track.slot, track.gen, args)
  }

  const dragRef = useRef<null | {
    note: number
    from: number
    startX: number
    startY: number
  }>(null)

  const header = (
    <div className="flex items-center gap-3 px-4 py-2 border-b border-groove-border">
      <span className="w-2.5 h-2.5 rounded-full" style={{ background: color }} />
      <span className="font-semibold text-groove-text">
        Edit — {KIND_NAMES[track.kind]} {track.slot}
      </span>
      <span className="text-xs text-groove-muted">
        {track.kind === KIND_MIDI_DRUM
          ? 'click a cell to toggle the hit'
          : 'click empty = add · drag = move · shift-click = delete'}
      </span>
      {!editable && (
        <span className="text-xs text-groove-yellow">(loading track data…)</span>
      )}
      <button
        onClick={onClose}
        className="ml-auto px-2 py-0.5 rounded bg-groove-border text-groove-text hover:bg-groove-red hover:text-white text-sm"
      >
        ✕ close
      </button>
    </div>
  )

  // -------------------------------------------------------------------------
  // Drum step grid
  // -------------------------------------------------------------------------
  if (track.kind === KIND_MIDI_DRUM) {
    const CELL_W = 34
    const hits = events.filter((e) => (e.status & 0xf0) === 0x90 && e.d2 > 0)

    return (
      <div className="bg-groove-panel border-2 rounded-lg" style={{ borderColor: color }}>
        {header}
        <div className="p-3 overflow-x-auto">
          <div className="inline-block">
            {/* step numbers */}
            <div className="flex ml-16">
              {Array.from({ length: steps }).map((_, s) => (
                <div
                  key={s}
                  className={`text-center text-[10px] ${
                    s % 4 === 0 ? 'text-groove-text' : 'text-groove-muted opacity-40'
                  }`}
                  style={{ width: CELL_W }}
                >
                  {s % 4 === 0 ? s / 4 + 1 : '·'}
                </div>
              ))}
            </div>
            {Array.from({ length: 8 }).map((_, rowFromTop) => {
              const row = 7 - rowFromTop // Rim on top, Kick at the bottom
              const note = 36 + row
              return (
                <div key={row} className="flex items-center">
                  <div className="w-16 text-right pr-2 text-xs text-groove-text font-mono">
                    {DRUM_NAMES[row]}
                  </div>
                  {Array.from({ length: steps }).map((_, s) => {
                    const cellStart = s * GRID
                    const hit = hits.find(
                      (e) => e.d1 === note && e.tick >= cellStart && e.tick < cellStart + GRID
                    )
                    const isBeat = s % 4 === 0
                    const isBar = s % 16 === 0
                    return (
                      <button
                        key={s}
                        disabled={!editable}
                        onClick={() =>
                          edit({
                            op: EDIT_TOGGLE_DRUM,
                            tick: hit ? hit.tick : cellStart,
                            note,
                            vel: 100,
                          })
                        }
                        className="h-7 rounded-sm border transition-colors"
                        style={{
                          width: CELL_W - 3,
                          marginRight: 3,
                          marginBottom: 3,
                          borderColor: isBar ? '#4d5566' : '#30363d',
                          background: hit
                            ? color
                            : isBeat
                              ? 'rgba(139,148,158,0.10)'
                              : 'transparent',
                          opacity: hit ? 0.35 + (hit.d2 / 127) * 0.65 : 1,
                          cursor: editable ? 'pointer' : 'default',
                        }}
                        title={hit ? `${DRUM_NAMES[row]} @ tick ${hit.tick} (vel ${hit.d2})` : ''}
                      />
                    )
                  })}
                </div>
              )
            })}
          </div>
        </div>
      </div>
    )
  }

  // -------------------------------------------------------------------------
  // Piano roll
  // -------------------------------------------------------------------------
  // pair ons with offs (wrap-aware like the lane)
  const ons = new Map<number, { tick: number; vel: number }>()
  const notes: Array<{ note: number; from: number; to: number; vel: number }> = []
  for (const e of events) {
    const type = e.status & 0xf0
    if (type === 0x90 && e.d2 > 0) {
      ons.set(e.d1, { tick: e.tick, vel: e.d2 })
    } else if ((type === 0x80 || (type === 0x90 && e.d2 === 0)) && ons.has(e.d1)) {
      const o = ons.get(e.d1)!
      notes.push({ note: e.d1, from: o.tick, to: e.tick, vel: o.vel })
      ons.delete(e.d1)
    }
  }
  for (const [note, o] of ons) {
    notes.push({ note, from: o.tick, to: o.tick + lengthTicks - 1, vel: o.vel })
  }

  let lo = 57
  let hi = 72
  for (const n of notes) {
    lo = Math.min(lo, n.note)
    hi = Math.max(hi, n.note)
  }
  lo = Math.max(0, lo - 4)
  hi = Math.min(127, hi + 4)
  const span = hi - lo + 1
  const ROW_H = 18
  const CELL_W = 34
  const W = steps * CELL_W
  const H = span * ROW_H

  const rowOf = (note: number) => hi - note // top row = highest note
  const posToNote = (y: number) => hi - Math.floor(y / ROW_H)
  const posToStep = (x: number) => Math.max(0, Math.min(Math.floor(x / CELL_W), steps - 1))

  const gridClick = (e: React.MouseEvent<HTMLDivElement>) => {
    if (!editable || dragRef.current) return
    const r = e.currentTarget.getBoundingClientRect()
    const note = posToNote(e.clientY - r.top)
    const step = posToStep(e.clientX - r.left)
    if (note < 0 || note > 127) return
    edit({ op: EDIT_ADD_NOTE, tick: step * GRID, note, vel: 96, dur16: 2 })
  }

  const beginDrag = (
    e: React.PointerEvent<HTMLDivElement>,
    note: number,
    from: number
  ) => {
    e.stopPropagation()
    if (!editable) return
    if (e.shiftKey) {
      edit({ op: EDIT_DELETE_NOTE, tick: from, note })
      return
    }
    dragRef.current = { note, from, startX: e.clientX, startY: e.clientY }
    e.currentTarget.setPointerCapture(e.pointerId)
  }

  const endDrag = (e: React.PointerEvent<HTMLDivElement>) => {
    const d = dragRef.current
    dragRef.current = null
    if (!d || !editable) return
    const dSteps = Math.round((e.clientX - d.startX) / CELL_W)
    const dNotes = -Math.round((e.clientY - d.startY) / ROW_H)
    if (dSteps === 0 && dNotes === 0) return
    let newTick = d.from + dSteps * GRID
    newTick = ((newTick % lengthTicks) + lengthTicks) % lengthTicks
    newTick = Math.round(newTick / GRID) * GRID
    if (newTick >= lengthTicks) newTick = 0
    const newNote = Math.max(0, Math.min(127, d.note + dNotes))
    edit({ op: EDIT_MOVE_NOTE, tick: d.from, note: d.note, newTick, newNote })
  }

  return (
    <div className="bg-groove-panel border-2 rounded-lg" style={{ borderColor: color }}>
      {header}
      <div className="p-3 overflow-auto" style={{ maxHeight: 480 }}>
        <div className="flex">
          {/* note-name gutter */}
          <div className="flex-shrink-0 w-12">
            <div style={{ height: 16 }} />
            {Array.from({ length: span }).map((_, r) => {
              const n = hi - r
              const isC = n % 12 === 0
              return (
                <div
                  key={r}
                  className={`text-right pr-2 font-mono text-[10px] leading-none flex items-center justify-end ${
                    isC ? 'text-groove-text font-bold' : 'text-groove-muted'
                  }`}
                  style={{ height: ROW_H }}
                >
                  {isC || n % 12 === 5 ? noteName(n) : ''}
                </div>
              )
            })}
          </div>
          <div className="flex-1 overflow-x-auto">
            {/* beat numbers */}
            <div className="flex" style={{ width: W, height: 16 }}>
              {Array.from({ length: steps }).map((_, s) => (
                <div
                  key={s}
                  className={`text-center text-[10px] ${
                    s % 4 === 0 ? 'text-groove-text' : 'text-groove-muted opacity-40'
                  }`}
                  style={{ width: CELL_W }}
                >
                  {s % 4 === 0 ? s / 4 + 1 : ''}
                </div>
              ))}
            </div>
            {/* the roll */}
            <div
              className="relative"
              style={{ width: W, height: H, cursor: editable ? 'crosshair' : 'default' }}
              onClick={gridClick}
            >
              {/* row stripes + gridlines */}
              {Array.from({ length: span }).map((_, r) => {
                const n = hi - r
                const black = [1, 3, 6, 8, 10].includes(n % 12)
                return (
                  <div
                    key={r}
                    className="absolute left-0 right-0"
                    style={{
                      top: r * ROW_H,
                      height: ROW_H,
                      background: black ? 'rgba(0,0,0,0.25)' : 'transparent',
                      borderBottom:
                        n % 12 === 0 ? '1px solid #4d5566' : '1px solid rgba(48,54,61,0.5)',
                    }}
                  />
                )
              })}
              {Array.from({ length: steps + 1 }).map((_, s) => (
                <div
                  key={s}
                  className="absolute top-0 bottom-0"
                  style={{
                    left: s * CELL_W,
                    width: 1,
                    background:
                      s % 16 === 0 ? '#4d5566' : s % 4 === 0 ? '#30363d' : 'rgba(48,54,61,0.4)',
                  }}
                />
              ))}
              {/* notes */}
              {notes.map((n, i) => {
                const segs =
                  n.to >= lengthTicks
                    ? [
                        [n.from, lengthTicks - 1],
                        [0, n.to - lengthTicks],
                      ]
                    : n.to < n.from
                      ? [
                          [n.from, lengthTicks - 1],
                          [0, n.to],
                        ]
                      : [[n.from, n.to]]
                return segs.map(([a, b], j) => (
                  <div
                    key={`${i}-${j}`}
                    className="absolute rounded"
                    onPointerDown={(e) => beginDrag(e, n.note, n.from)}
                    onPointerUp={endDrag}
                    style={{
                      left: (a / lengthTicks) * W + 1,
                      top: rowOf(n.note) * ROW_H + 2,
                      width: Math.max(((b - a) / lengthTicks) * W - 2, 10),
                      height: ROW_H - 4,
                      background: color,
                      opacity: 0.45 + (n.vel / 127) * 0.55,
                      cursor: editable ? 'grab' : 'default',
                      border: '1px solid rgba(255,255,255,0.25)',
                    }}
                    title={`${noteName(n.note)} @ tick ${n.from} (vel ${n.vel}) — drag to move, shift-click to delete`}
                  />
                ))
              })}
            </div>
          </div>
        </div>
      </div>
    </div>
  )
}
