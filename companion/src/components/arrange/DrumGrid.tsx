/**
 * DrumGrid - 8-lane drum pattern visualization
 *
 * Shows drum hits as dots on a grid with:
 * - 8 rows (one per drum)
 * - Columns representing subdivisions (16th notes per bar)
 * - Playhead position indicator
 */

import { PatternEvent } from '../../core/protocol'

// Drum names matching sampler.h order
const DRUM_NAMES = ['Kick', 'Snare', 'HH-C', 'HH-O', 'Clap', 'Tom Lo', 'Tom Md', 'Rim']

// Constants matching sequencer.h
const PPQN = 96
const TICKS_PER_BAR = PPQN * 4  // 384
const PATTERN_BARS = 4
const PATTERN_TICKS = TICKS_PER_BAR * PATTERN_BARS  // 1536

// Grid resolution - 16th notes
const STEPS_PER_BAR = 16
const TICKS_PER_STEP = TICKS_PER_BAR / STEPS_PER_BAR  // 24 ticks

interface DrumGridProps {
  /** Events for all 8 drum tracks (notes 36-43) */
  events: PatternEvent[][]  // 8 tracks
  /** Pending events during recording (not yet confirmed by Daisy) */
  pendingEvents: PatternEvent[][]  // 8 tracks
  /** Current playhead position in ticks */
  playheadTick: number
  /** Whether transport is playing */
  playing: boolean
}

export default function DrumGrid({ events, pendingEvents, playheadTick, playing }: DrumGridProps) {
  const totalSteps = STEPS_PER_BAR * PATTERN_BARS
  const currentStep = Math.floor((playheadTick % PATTERN_TICKS) / TICKS_PER_STEP)

  // Convert events to step positions (returns { step, pending } objects)
  const getHitsForTrack = (trackEvents: PatternEvent[], trackPendingEvents: PatternEvent[]): Array<{ step: number; pending: boolean }> => {
    const confirmedHits = trackEvents
      .filter(e => (e.status & 0xF0) === 0x90 && e.data2 > 0)
      .map(e => ({ step: Math.floor(e.tick / TICKS_PER_STEP), pending: false }))

    const pendingHits = trackPendingEvents
      .filter(e => (e.status & 0xF0) === 0x90 && e.data2 > 0)
      .map(e => ({ step: Math.floor(e.tick / TICKS_PER_STEP), pending: true }))

    return [...confirmedHits, ...pendingHits]
  }

  return (
    <div className="bg-groove-panel rounded-lg border border-groove-border p-3">
      <div className="flex items-center justify-between mb-2">
        <h3 className="text-sm font-semibold text-groove-text">Drums</h3>
        <span className="text-xs text-groove-text-dim">
          {events.reduce((sum, track) => sum + track.length, 0)} events
        </span>
      </div>

      <div className="overflow-x-auto">
        <div className="min-w-[600px]">
          {/* Header - bar numbers */}
          <div className="flex mb-1">
            <div className="w-16 shrink-0" />
            {Array.from({ length: PATTERN_BARS }).map((_, bar) => (
              <div key={bar} className="flex-1 text-center text-xs text-groove-text-dim">
                Bar {bar + 1}
              </div>
            ))}
          </div>

          {/* Drum lanes */}
          {DRUM_NAMES.map((name, trackIdx) => {
            const hits = getHitsForTrack(events[trackIdx] || [], pendingEvents[trackIdx] || [])

            return (
              <div key={trackIdx} className="flex items-center mb-0.5">
                {/* Track name */}
                <div className="w-16 shrink-0 text-xs text-groove-text-dim pr-2 text-right">
                  {name}
                </div>

                {/* Step grid */}
                <div className="flex-1 flex gap-px">
                  {Array.from({ length: totalSteps }).map((_, step) => {
                    const hit = hits.find(h => h.step === step)
                    const hasHit = !!hit
                    const isPending = hit?.pending
                    const isPlayhead = playing && step === currentStep
                    const isDownbeat = step % STEPS_PER_BAR === 0
                    const isBeat = step % 4 === 0

                    return (
                      <div
                        key={step}
                        className={`h-4 flex-1 rounded-sm flex items-center justify-center ${
                          isPlayhead
                            ? 'bg-groove-accent'
                            : isDownbeat
                            ? 'bg-groove-border/80'
                            : isBeat
                            ? 'bg-groove-border/50'
                            : 'bg-groove-border/30'
                        }`}
                      >
                        {hasHit && (
                          <div className={`w-2 h-2 rounded-full ${
                            isPlayhead ? 'bg-white' :
                            isPending ? 'bg-orange-400 animate-pulse' : 'bg-groove-accent'
                          }`} />
                        )}
                      </div>
                    )
                  })}
                </div>
              </div>
            )
          })}
        </div>
      </div>
    </div>
  )
}
