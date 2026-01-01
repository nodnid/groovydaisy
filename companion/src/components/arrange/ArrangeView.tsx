/**
 * ArrangeView - Main arrange tab view
 *
 * Displays:
 * - Resource meters (memory + CPU)
 * - 4 synth track lanes with piano roll
 * - 8 drum track lanes with grid
 * - Track controls (mute/solo/freeze)
 */

import { PatternEvent, TrackStatus } from '../../core/protocol'
import ResourceMeter from './ResourceMeter'
import PianoRoll from './PianoRoll'
import DrumGrid from './DrumGrid'

interface SynthTrackState {
  id: number
  status: TrackStatus
  frozenSlot: number
  events: PatternEvent[]
  pendingEvents: PatternEvent[]  // Real-time events during recording
}

interface ArrangeViewProps {
  /** Memory usage in bytes */
  memoryUsed: number
  /** Total memory in bytes */
  memoryTotal: number
  /** CPU load 0-100 */
  cpuLoad: number
  /** Pattern events for synth tracks (4 tracks, index 8-11 in sequencer) */
  synthTracks: SynthTrackState[]
  /** Pattern events for drum tracks (8 tracks, index 0-7 in sequencer) */
  drumEvents: PatternEvent[][]
  /** Pending drum events during recording */
  pendingDrumEvents: PatternEvent[][]
  /** Current playhead tick */
  playheadTick: number
  /** Whether playing */
  playing: boolean
  /** Connected to device */
  connected: boolean
  /** Request freeze for a synth track */
  onFreezeTrack?: (trackId: number) => void
  /** Request unfreeze for a synth track */
  onUnfreezeTrack?: (trackId: number) => void
}

function StatusBadge({ status }: { status: TrackStatus }) {
  switch (status) {
    case TrackStatus.PENDING:
      return (
        <span className="px-1.5 py-0.5 text-xs rounded bg-orange-500/20 text-orange-400 animate-pulse">
          Pending...
        </span>
      )
    case TrackStatus.RENDERING:
      return (
        <span className="px-1.5 py-0.5 text-xs rounded bg-yellow-500/20 text-yellow-400 animate-pulse">
          Rendering...
        </span>
      )
    case TrackStatus.AUDIO:
      return (
        <span className="px-1.5 py-0.5 text-xs rounded bg-blue-500/20 text-blue-400">
          Frozen
        </span>
      )
    default:
      return (
        <span className="px-1.5 py-0.5 text-xs rounded bg-groove-border text-groove-text-dim">
          MIDI
        </span>
      )
  }
}

export default function ArrangeView({
  memoryUsed,
  memoryTotal,
  cpuLoad,
  synthTracks,
  drumEvents,
  pendingDrumEvents,
  playheadTick,
  playing,
  connected,
  onFreezeTrack,
  onUnfreezeTrack,
}: ArrangeViewProps) {
  return (
    <div className="space-y-4">
      {/* Resource Meters */}
      <div className="bg-groove-panel rounded-lg border border-groove-border p-3">
        <ResourceMeter
          memoryUsed={memoryUsed}
          memoryTotal={memoryTotal}
          cpuLoad={cpuLoad}
        />
      </div>

      {/* Synth Tracks */}
      <div className="bg-groove-panel rounded-lg border border-groove-border p-4">
        <h2 className="text-sm font-semibold text-groove-text mb-3">Synth Tracks</h2>
        <div className="space-y-2">
          {synthTracks.map((track, idx) => (
            <div key={idx} className="flex gap-2">
              {/* Track controls */}
              <div className="w-24 shrink-0 flex flex-col gap-1">
                <StatusBadge status={track.status} />
                {track.status === TrackStatus.MIDI ? (
                  <button
                    onClick={() => onFreezeTrack?.(track.id)}
                    disabled={!connected}
                    className="px-2 py-1 text-xs rounded bg-groove-border hover:bg-groove-accent/20 text-groove-text disabled:opacity-50"
                  >
                    Freeze
                  </button>
                ) : track.status === TrackStatus.AUDIO ? (
                  <button
                    onClick={() => onUnfreezeTrack?.(track.id)}
                    disabled={!connected}
                    className="px-2 py-1 text-xs rounded bg-groove-border hover:bg-groove-red/20 text-groove-text disabled:opacity-50"
                  >
                    Unfreeze
                  </button>
                ) : null}
              </div>

              {/* Piano roll */}
              <div className="flex-1">
                <PianoRoll
                  events={track.events}
                  pendingEvents={track.pendingEvents}
                  playheadTick={playheadTick}
                  playing={playing}
                  trackNum={idx + 1}
                />
              </div>
            </div>
          ))}
        </div>
      </div>

      {/* Drum Tracks */}
      <DrumGrid
        events={drumEvents}
        pendingEvents={pendingDrumEvents}
        playheadTick={playheadTick}
        playing={playing}
      />
    </div>
  )
}
