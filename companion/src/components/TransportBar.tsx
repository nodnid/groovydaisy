import { useState, useEffect, useCallback, useRef } from 'react'
import { PPQN, TICKS_PER_BAR, MIN_BPM, MAX_BPM } from '../core/constants'
import { interpolateTick, type SyncRef, type TransportState } from '../core/state'

export type { TransportState }

interface TransportBarProps {
  transport: TransportState
  sync: SyncRef
  onPlay: () => void
  onStop: () => void
  onRewind: () => void
  onTap: () => void
  onTempoChange: (bpm: number) => void
  connected: boolean
}

/**
 * Transport controls + interpolated playhead.
 *
 * v2: the firmware sends MSG_SYNC ~5x/sec; the displayed position is
 * interpolated between syncs from the tempo (no tick streaming). There is
 * no pattern in v2 — the bar count is unbounded, and the progress strip
 * shows beats within the current bar.
 */
export default function TransportBar({
  transport,
  sync,
  onPlay,
  onStop,
  onRewind,
  onTap,
  onTempoChange,
  connected,
}: TransportBarProps) {
  const [tempoBpm, setTempoBpm] = useState(transport.bpm)
  const [isEditingTempo, setIsEditingTempo] = useState(false)
  const [displayTick, setDisplayTick] = useState(0)
  const rafRef = useRef<number>(0)

  // Sync local tempo with transport state
  useEffect(() => {
    if (!isEditingTempo) {
      setTempoBpm(transport.bpm)
    }
  }, [transport.bpm, isEditingTempo])

  // Playhead interpolation loop (only runs while playing)
  useEffect(() => {
    if (!transport.playing) {
      setDisplayTick(sync.tick)
      return
    }
    const step = () => {
      setDisplayTick(interpolateTick(sync, transport, performance.now(), PPQN))
      rafRef.current = requestAnimationFrame(step)
    }
    rafRef.current = requestAnimationFrame(step)
    return () => cancelAnimationFrame(rafRef.current)
  }, [transport, sync])

  const handleTempoSubmit = useCallback(() => {
    const bpm = Math.max(MIN_BPM, Math.min(MAX_BPM, tempoBpm))
    setTempoBpm(bpm)
    onTempoChange(bpm)
    setIsEditingTempo(false)
  }, [tempoBpm, onTempoChange])

  const handleTempoKeyDown = useCallback(
    (e: React.KeyboardEvent) => {
      if (e.key === 'Enter') {
        handleTempoSubmit()
      } else if (e.key === 'Escape') {
        setTempoBpm(transport.bpm)
        setIsEditingTempo(false)
      }
    },
    [handleTempoSubmit, transport.bpm]
  )

  const adjustTempo = useCallback(
    (delta: number) => {
      const newBpm = Math.max(MIN_BPM, Math.min(MAX_BPM, transport.bpm + delta))
      onTempoChange(newBpm)
    },
    [transport.bpm, onTempoChange]
  )

  const bar = Math.floor(displayTick / TICKS_PER_BAR) + 1
  const beat = Math.floor((displayTick % TICKS_PER_BAR) / PPQN) + 1
  const beatProgress = ((displayTick % TICKS_PER_BAR) / TICKS_PER_BAR) * 100
  const bpmDisplay = Number.isInteger(transport.bpm)
    ? transport.bpm.toString()
    : transport.bpm.toFixed(1)

  return (
    <div className="bg-groove-panel border border-groove-border rounded-lg">
      <div className="px-4 py-3 flex items-center justify-between gap-4">
        {/* Transport Controls */}
        <div className="flex items-center gap-2">
          {/* Play/Stop */}
          {transport.playing ? (
            <button
              onClick={onStop}
              disabled={!connected}
              className="w-12 h-12 rounded-lg bg-groove-green hover:bg-green-400 disabled:opacity-50 disabled:cursor-not-allowed flex items-center justify-center transition-colors"
              title="Stop"
            >
              <svg className="w-6 h-6 text-white" fill="currentColor" viewBox="0 0 24 24">
                <rect x="6" y="6" width="12" height="12" />
              </svg>
            </button>
          ) : (
            <button
              onClick={onPlay}
              disabled={!connected}
              className="w-12 h-12 rounded-lg bg-groove-border hover:bg-groove-accent disabled:opacity-50 disabled:cursor-not-allowed flex items-center justify-center transition-colors"
              title="Play"
            >
              <svg className="w-6 h-6 text-white" fill="currentColor" viewBox="0 0 24 24">
                <polygon points="8,5 19,12 8,19" />
              </svg>
            </button>
          )}

          {/* Rewind */}
          <button
            onClick={onRewind}
            disabled={!connected}
            className="w-12 h-12 rounded-lg bg-groove-border hover:bg-groove-accent disabled:opacity-50 disabled:cursor-not-allowed flex items-center justify-center transition-colors"
            title="Rewind to bar 1 (does not clear anything)"
          >
            <svg className="w-6 h-6 text-white" fill="currentColor" viewBox="0 0 24 24">
              <polygon points="19,5 8,12 19,19" />
              <rect x="5" y="5" width="3" height="14" />
            </svg>
          </button>
        </div>

        {/* Position Display */}
        <div className="flex-1 flex items-center justify-center">
          <div className="text-center">
            <div className="text-3xl font-mono font-bold text-groove-text">
              {bar}.{beat}
            </div>
            <div className="text-xs text-groove-muted">BAR.BEAT</div>
          </div>
        </div>

        {/* Tempo Control */}
        <div className="flex items-center gap-2">
          <button
            onClick={onTap}
            disabled={!connected || transport.tempoLocked}
            className="px-3 h-8 rounded bg-groove-border hover:bg-groove-accent disabled:opacity-50 disabled:cursor-not-allowed text-groove-text text-sm font-semibold"
            title="Tap tempo"
          >
            TAP
          </button>

          <button
            onClick={() => adjustTempo(-1)}
            disabled={!connected || transport.tempoLocked}
            className="w-8 h-8 rounded bg-groove-border hover:bg-groove-accent disabled:opacity-50 disabled:cursor-not-allowed flex items-center justify-center"
            title="Decrease tempo"
          >
            <span className="text-groove-text font-bold">-</span>
          </button>

          <div className="text-center min-w-[80px]">
            {isEditingTempo ? (
              <input
                type="number"
                value={tempoBpm}
                onChange={(e) => setTempoBpm(parseFloat(e.target.value) || 120)}
                onKeyDown={handleTempoKeyDown}
                onBlur={handleTempoSubmit}
                autoFocus
                className="w-16 text-2xl font-bold text-center bg-groove-bg border border-groove-accent rounded text-groove-text"
                min={MIN_BPM}
                max={MAX_BPM}
              />
            ) : (
              <div
                onClick={() => connected && !transport.tempoLocked && setIsEditingTempo(true)}
                className={`text-2xl font-bold text-groove-text ${
                  connected && !transport.tempoLocked
                    ? 'cursor-pointer hover:text-groove-accent'
                    : ''
                }`}
                title={
                  transport.tempoLocked
                    ? 'Tempo locked — delete all audio loops to unlock'
                    : 'Click to edit tempo'
                }
              >
                {transport.tempoLocked && <span title="Tempo locked">🔒 </span>}
                {bpmDisplay}
              </div>
            )}
            <div className="text-xs text-groove-muted">BPM</div>
          </div>

          <button
            onClick={() => adjustTempo(1)}
            disabled={!connected || transport.tempoLocked}
            className="w-8 h-8 rounded bg-groove-border hover:bg-groove-accent disabled:opacity-50 disabled:cursor-not-allowed flex items-center justify-center"
            title="Increase tempo"
          >
            <span className="text-groove-text font-bold">+</span>
          </button>
        </div>
      </div>

      {/* Beat strip: position within the current bar */}
      <div className="h-6 relative bg-groove-bg border-t border-groove-border rounded-b-lg overflow-hidden">
        {/* Beat cells */}
        {[1, 2, 3, 4].map((b) => (
          <div
            key={b}
            className="absolute top-0 bottom-0 flex items-center justify-center text-xs border-r border-groove-border last:border-r-0"
            style={{ left: `${((b - 1) / 4) * 100}%`, width: '25%' }}
          >
            <span
              className={
                beat === b && transport.playing
                  ? 'text-groove-text font-bold'
                  : 'text-groove-muted'
              }
            >
              {b}
            </span>
          </div>
        ))}
        {/* Playhead — no CSS transition, must track interpolation exactly */}
        {transport.playing && (
          <div
            className="absolute top-0 bottom-0 w-0.5 bg-groove-accent"
            style={{ left: `${beatProgress}%` }}
          />
        )}
      </div>
    </div>
  )
}
