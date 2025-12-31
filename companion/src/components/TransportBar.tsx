import { useState, useEffect, useCallback } from 'react'

export interface TransportState {
  playing: boolean
  recording: boolean
  bpm: number
}

interface TransportBarProps {
  transport: TransportState
  position: { bar: number; beat: number; tick: number }
  onPlay: () => void
  onStop: () => void
  onRecord: () => void
  onTempoChange: (bpm: number) => void
  connected: boolean
}

export default function TransportBar({
  transport,
  position,
  onPlay,
  onStop,
  onRecord,
  onTempoChange,
  connected,
}: TransportBarProps) {
  const [tempoBpm, setTempoBpm] = useState(transport.bpm)
  const [isEditingTempo, setIsEditingTempo] = useState(false)

  // Sync local tempo with transport state
  useEffect(() => {
    if (!isEditingTempo) {
      setTempoBpm(transport.bpm)
    }
  }, [transport.bpm, isEditingTempo])

  const handleTempoSubmit = useCallback(() => {
    const bpm = Math.max(30, Math.min(300, tempoBpm))
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

  const handleTempoBlur = useCallback(() => {
    handleTempoSubmit()
  }, [handleTempoSubmit])

  const adjustTempo = useCallback(
    (delta: number) => {
      const newBpm = Math.max(30, Math.min(300, transport.bpm + delta))
      onTempoChange(newBpm)
    },
    [transport.bpm, onTempoChange]
  )

  // Calculate progress through the 4-bar pattern
  // position.tick is already wrapped to 0-1535 (4 bars * 384 ticks/bar)
  const PATTERN_TICKS = 1536  // 4 bars * 96 PPQN * 4 beats
  const progressPercent = (position.tick / PATTERN_TICKS) * 100

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
              title="Stop (double-click to reset)"
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

          {/* Record */}
          <button
            onClick={onRecord}
            disabled={!connected}
            className={`w-12 h-12 rounded-lg flex items-center justify-center transition-colors disabled:opacity-50 disabled:cursor-not-allowed ${
              transport.recording
                ? 'bg-groove-red animate-pulse'
                : 'bg-groove-border hover:bg-red-700'
            }`}
            title={transport.recording ? 'Stop Recording' : 'Start Recording'}
          >
            <svg className="w-6 h-6 text-white" fill="currentColor" viewBox="0 0 24 24">
              <circle cx="12" cy="12" r="6" />
            </svg>
          </button>
        </div>

        {/* Position Display */}
        <div className="flex-1 flex items-center justify-center">
          <div className="text-center">
            <div className="text-3xl font-mono font-bold text-groove-text">
              {position.bar}.{position.beat}
            </div>
            <div className="text-xs text-groove-muted">BAR.BEAT</div>
          </div>
        </div>

        {/* Tempo Control */}
        <div className="flex items-center gap-2">
          <button
            onClick={() => adjustTempo(-1)}
            disabled={!connected}
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
                onChange={(e) => setTempoBpm(parseInt(e.target.value) || 120)}
                onKeyDown={handleTempoKeyDown}
                onBlur={handleTempoBlur}
                autoFocus
                className="w-16 text-2xl font-bold text-center bg-groove-bg border border-groove-accent rounded text-groove-text"
                min={30}
                max={300}
              />
            ) : (
              <div
                onClick={() => connected && setIsEditingTempo(true)}
                className={`text-2xl font-bold text-groove-text ${
                  connected ? 'cursor-pointer hover:text-groove-accent' : ''
                }`}
                title="Click to edit tempo"
              >
                {transport.bpm}
              </div>
            )}
            <div className="text-xs text-groove-muted">BPM</div>
          </div>

          <button
            onClick={() => adjustTempo(1)}
            disabled={!connected}
            className="w-8 h-8 rounded bg-groove-border hover:bg-groove-accent disabled:opacity-50 disabled:cursor-not-allowed flex items-center justify-center"
            title="Increase tempo"
          >
            <span className="text-groove-text font-bold">+</span>
          </button>
        </div>
      </div>

      {/* Progress Bar - no transition to stay in sync with playhead */}
      <div className="h-1 bg-groove-border">
        <div
          className={transport.recording ? 'h-full bg-groove-red' : 'h-full bg-groove-accent'}
          style={{ width: transport.playing ? `${progressPercent}%` : '0%' }}
        />
      </div>

      {/* Bar Markers */}
      <div className="h-6 relative bg-groove-bg border-t border-groove-border">
        {/* Vertical dividers between bars */}
        {[1, 2, 3].map((i) => (
          <div
            key={`div-${i}`}
            className="absolute top-0 bottom-0 w-px bg-groove-border"
            style={{ left: `${(i / 4) * 100}%` }}
          />
        ))}
        {/* Bar labels */}
        {[1, 2, 3, 4].map((bar) => (
          <div
            key={bar}
            className="absolute top-0 bottom-0 flex items-center justify-center text-xs text-groove-muted"
            style={{ left: `${((bar - 1) / 4) * 100}%`, width: '25%' }}
          >
            <span
              className={
                position.bar === bar && transport.playing
                  ? 'text-groove-text font-bold'
                  : ''
              }
            >
              {bar}
            </span>
          </div>
        ))}
        {/* Playhead */}
        {transport.playing && (
          <div
            className="absolute top-0 bottom-0 w-0.5 bg-groove-accent"
            style={{ left: `${progressPercent}%` }}
          />
        )}
      </div>
    </div>
  )
}
