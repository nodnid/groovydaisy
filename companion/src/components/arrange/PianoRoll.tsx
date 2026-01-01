/**
 * PianoRoll - Note visualization for synth tracks
 *
 * Shows MIDI notes as horizontal bars:
 * - Y axis: note pitch (restricted to notes present in pattern)
 * - X axis: time
 * - Bar width: note duration
 * - Playhead indicator
 */

import { PatternEvent } from '../../core/protocol'

// Constants matching sequencer.h
const PPQN = 96
const TICKS_PER_BAR = PPQN * 4  // 384
const PATTERN_BARS = 4
const PATTERN_TICKS = TICKS_PER_BAR * PATTERN_BARS  // 1536

// Note names
const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']

function midiNoteToName(note: number): string {
  const octave = Math.floor(note / 12) - 1
  const name = NOTE_NAMES[note % 12]
  return `${name}${octave}`
}

interface NoteRect {
  note: number
  startTick: number
  duration: number
  velocity: number
  pending?: boolean  // True if this is a real-time event not yet confirmed
}

interface PianoRollProps {
  /** Events for this synth track */
  events: PatternEvent[]
  /** Pending events during recording (not yet confirmed by Daisy) */
  pendingEvents: PatternEvent[]
  /** Current playhead position in ticks */
  playheadTick: number
  /** Whether transport is playing */
  playing: boolean
  /** Track number (for display) */
  trackNum: number
}

export default function PianoRoll({ events, pendingEvents, playheadTick, playing, trackNum }: PianoRollProps) {
  // Convert NoteOn/NoteOff pairs to note rectangles
  const notes: NoteRect[] = []

  // Helper to process events into notes
  const processEvents = (evts: PatternEvent[], pending: boolean) => {
    const activeNotes = new Map<number, { startTick: number; velocity: number }>()
    const sortedEvents = [...evts].sort((a, b) => a.tick - b.tick)

    for (const event of sortedEvents) {
      const type = event.status & 0xF0
      const note = event.data1

      if (type === 0x90 && event.data2 > 0) {
        // NoteOn
        activeNotes.set(note, { startTick: event.tick, velocity: event.data2 })
      } else if (type === 0x80 || (type === 0x90 && event.data2 === 0)) {
        // NoteOff
        const startInfo = activeNotes.get(note)
        if (startInfo) {
          notes.push({
            note,
            startTick: startInfo.startTick,
            duration: event.tick - startInfo.startTick,
            velocity: startInfo.velocity,
            pending,
          })
          activeNotes.delete(note)
        }
      }
    }

    // Handle notes still active at end of pattern (wrap to pattern start)
    activeNotes.forEach((startInfo, note) => {
      notes.push({
        note,
        startTick: startInfo.startTick,
        duration: PATTERN_TICKS - startInfo.startTick,
        velocity: startInfo.velocity,
        pending,
      })
    })
  }

  // Process confirmed events first, then pending events
  processEvents(events, false)
  processEvents(pendingEvents, true)

  // Find note range for display
  const allNotes = notes.map(n => n.note)
  const minNote = allNotes.length > 0 ? Math.min(...allNotes) : 60
  const maxNote = allNotes.length > 0 ? Math.max(...allNotes) : 72
  const noteRange = maxNote - minNote + 1
  const displayRows = Math.max(noteRange, 6)  // At least 6 rows for visibility

  // Adjust range to center notes
  const padding = Math.floor((displayRows - noteRange) / 2)
  const startNote = minNote - padding
  const endNote = startNote + displayRows

  const playheadPct = (playheadTick % PATTERN_TICKS) / PATTERN_TICKS * 100

  if (events.length === 0) {
    return (
      <div className="bg-groove-panel/50 rounded border border-groove-border/50 p-2 flex items-center justify-center text-groove-text-dim text-xs h-20">
        Track {trackNum} - Empty
      </div>
    )
  }

  return (
    <div className="bg-groove-panel rounded border border-groove-border overflow-hidden">
      <div className="flex items-center justify-between px-2 py-1 border-b border-groove-border/50">
        <span className="text-xs font-medium text-groove-text">Track {trackNum}</span>
        <span className="text-xs text-groove-text-dim">{notes.length} notes</span>
      </div>

      <div className="relative h-24">
        {/* Note labels on left */}
        <div className="absolute left-0 top-0 bottom-0 w-8 bg-groove-panel/80 z-10 border-r border-groove-border/50">
          {Array.from({ length: displayRows }).map((_, i) => {
            const note = endNote - i - 1
            return (
              <div
                key={i}
                className="text-[8px] text-groove-text-dim text-right pr-1 leading-none"
                style={{ height: `${100 / displayRows}%` }}
              >
                {midiNoteToName(note)}
              </div>
            )
          })}
        </div>

        {/* Grid background */}
        <div className="absolute left-8 right-0 top-0 bottom-0">
          {/* Bar lines */}
          {Array.from({ length: PATTERN_BARS + 1 }).map((_, i) => (
            <div
              key={i}
              className="absolute top-0 bottom-0 w-px bg-groove-border/50"
              style={{ left: `${(i / PATTERN_BARS) * 100}%` }}
            />
          ))}

          {/* Row lines */}
          {Array.from({ length: displayRows + 1 }).map((_, i) => (
            <div
              key={i}
              className="absolute left-0 right-0 h-px bg-groove-border/30"
              style={{ top: `${(i / displayRows) * 100}%` }}
            />
          ))}

          {/* Note rectangles */}
          {notes.map((note, i) => {
            const row = endNote - note.note - 1
            if (row < 0 || row >= displayRows) return null

            const left = (note.startTick / PATTERN_TICKS) * 100
            const width = (note.duration / PATTERN_TICKS) * 100
            const opacity = 0.5 + (note.velocity / 127) * 0.5

            return (
              <div
                key={i}
                className={`absolute rounded-sm ${
                  note.pending ? 'bg-orange-400 animate-pulse' : 'bg-groove-accent'
                }`}
                style={{
                  left: `${left}%`,
                  width: `${width}%`,
                  top: `${(row / displayRows) * 100}%`,
                  height: `${100 / displayRows}%`,
                  opacity,
                }}
              />
            )
          })}

          {/* Playhead */}
          {playing && (
            <div
              className="absolute top-0 bottom-0 w-0.5 bg-white z-20"
              style={{ left: `${playheadPct}%` }}
            />
          )}
        </div>
      </div>
    </div>
  )
}
