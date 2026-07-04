import { useEffect, useRef, useState } from 'react'
import { PPQN, TICKS_PER_BAR } from '../core/constants'
import {
  interpolateTick,
  type SyncRef,
  type TransportState,
  type TrackState,
  type StripState,
  type CaptureFlash,
} from '../core/state'
import {
  SRC_PADS,
  SRC_KEYS,
  SOURCE_NAMES,
  CAP_PENDING,
  CAP_COMMITTED,
  CAP_REFUSED,
  KIND_MIDI_DRUM,
  KIND_MIDI_SYNTH,
  ERROR_NAMES,
} from '../core/protocol'

interface TrackListProps {
  tracks: Record<number, TrackState>
  strips: Record<number, StripState>
  transport: TransportState
  sync: SyncRef
  captureFlash: CaptureFlash | null
  onCapture: (source: number, bars: number) => void
  onUndo: () => void
  onDelete: (slot: number, gen: number) => void
  onMute: (slot: number, mute: boolean) => void
  onSrcLen: (source: number, bars: number) => void
  connected: boolean
}

const KIND_COLORS: Record<number, string> = {
  [KIND_MIDI_DRUM]: '#3fb950', // green
  [KIND_MIDI_SYNTH]: '#58a6ff', // blue
  2: '#d29922', // audio: yellow (Phase 3)
}

const KIND_NAMES: Record<number, string> = {
  [KIND_MIDI_DRUM]: 'Drums',
  [KIND_MIDI_SYNTH]: 'Synth',
  2: 'Audio',
}

const BAR_CHOICES = [1, 2, 4, 8]

/** Circular loop ring with a rotating playhead. */
function LoopRing({
  track,
  muted,
  nowTick,
  playing,
}: {
  track: TrackState
  muted: boolean
  nowTick: number
  playing: boolean
}) {
  const lengthTicks = track.lengthBars * TICKS_PER_BAR
  const phase = (nowTick % lengthTicks) / lengthTicks
  const r = 26
  const c = 2 * Math.PI * r
  const color = KIND_COLORS[track.kind] ?? '#8b949e'

  return (
    <svg viewBox="0 0 64 64" className="w-16 h-16">
      {/* base ring */}
      <circle
        cx="32"
        cy="32"
        r={r}
        fill="none"
        stroke={muted ? '#30363d' : color}
        strokeOpacity={muted ? 1 : 0.35}
        strokeWidth="6"
      />
      {/* progress arc */}
      {playing && !muted && (
        <circle
          cx="32"
          cy="32"
          r={r}
          fill="none"
          stroke={color}
          strokeWidth="6"
          strokeDasharray={`${phase * c} ${c}`}
          transform="rotate(-90 32 32)"
          strokeLinecap="round"
        />
      )}
      {/* bar count */}
      <text
        x="32"
        y="36"
        textAnchor="middle"
        fontSize="14"
        fontWeight="bold"
        fill={muted ? '#8b949e' : '#c9d1d9'}
      >
        {track.lengthBars}
      </text>
    </svg>
  )
}

/** Hold-to-delete button: hold 600 ms to confirm (SPEC: the gesture is the safety). */
function HoldDeleteButton({
  onDelete,
  disabled,
}: {
  onDelete: () => void
  disabled: boolean
}) {
  const [holding, setHolding] = useState(false)
  const timerRef = useRef<number | null>(null)

  const start = () => {
    if (disabled) return
    setHolding(true)
    timerRef.current = window.setTimeout(() => {
      setHolding(false)
      onDelete()
    }, 600)
  }
  const cancel = () => {
    setHolding(false)
    if (timerRef.current !== null) {
      clearTimeout(timerRef.current)
      timerRef.current = null
    }
  }

  return (
    <button
      onMouseDown={start}
      onMouseUp={cancel}
      onMouseLeave={cancel}
      onTouchStart={start}
      onTouchEnd={cancel}
      disabled={disabled}
      className={`px-2 py-0.5 rounded text-xs transition-colors disabled:opacity-50 ${
        holding ? 'bg-groove-red text-white' : 'bg-groove-border text-groove-muted hover:text-groove-red'
      }`}
      title="Hold to delete"
    >
      {holding ? 'HOLD…' : '✕'}
    </button>
  )
}

export default function TrackList({
  tracks,
  strips,
  transport,
  sync,
  captureFlash,
  onCapture,
  onUndo,
  onDelete,
  onMute,
  onSrcLen,
  connected,
}: TrackListProps) {
  const [nowTick, setNowTick] = useState(0)
  const [padBars, setPadBars] = useState(4)
  const [keyBars, setKeyBars] = useState(4)
  const rafRef = useRef<number>(0)

  // One animation loop drives every ring
  useEffect(() => {
    if (!transport.playing) {
      setNowTick(sync.tick)
      return
    }
    const step = () => {
      setNowTick(interpolateTick(sync, transport, performance.now(), PPQN))
      rafRef.current = requestAnimationFrame(step)
    }
    rafRef.current = requestAnimationFrame(step)
    return () => cancelAnimationFrame(rafRef.current)
  }, [transport, sync])

  const ordered = Object.values(tracks).sort((a, b) => a.createdSeq - b.createdSeq)

  const flashText = (() => {
    if (!captureFlash) return null
    const src = SOURCE_NAMES[captureFlash.source] ?? '?'
    switch (captureFlash.status) {
      case CAP_PENDING:
        return `⏳ Waiting for the bar line… (${src}, ${captureFlash.bars} bars)`
      case CAP_COMMITTED:
        return `✓ Grabbed ${captureFlash.bars} bars of ${src} → track ${captureFlash.slot}`
      case CAP_REFUSED:
        return `✗ Capture refused: ${ERROR_NAMES[captureFlash.reason] ?? 'unknown'}`
      default:
        return null
    }
  })()

  const lenSelect = (
    value: number,
    setValue: (n: number) => void,
    source: number
  ) => (
    <select
      value={value}
      disabled={!connected}
      onChange={(e) => {
        const bars = parseInt(e.target.value)
        setValue(bars)
        onSrcLen(source, bars)
      }}
      className="bg-groove-bg border border-groove-border rounded text-groove-text text-xs px-1 py-0.5"
      title="Capture length in bars"
    >
      {BAR_CHOICES.map((b) => (
        <option key={b} value={b}>
          {b} bar{b > 1 ? 's' : ''}
        </option>
      ))}
    </select>
  )

  return (
    <div className="bg-groove-panel border border-groove-border rounded-lg">
      <div className="px-4 py-3 border-b border-groove-border flex items-center justify-between flex-wrap gap-2">
        <h2 className="font-semibold text-groove-text">Loop Tracks</h2>

        {/* Capture controls */}
        <div className="flex items-center gap-2 flex-wrap">
          <button
            onClick={() => onCapture(SRC_PADS, padBars)}
            disabled={!connected || !transport.playing}
            className="px-3 py-1 rounded bg-groove-green hover:bg-green-500 disabled:opacity-50 text-white text-sm font-semibold"
            title={transport.playing ? 'Grab the last bars of pad playing' : 'Press play first'}
          >
            ⏺ Grab Pads
          </button>
          {lenSelect(padBars, setPadBars, SRC_PADS)}

          <button
            onClick={() => onCapture(SRC_KEYS, keyBars)}
            disabled={!connected || !transport.playing}
            className="px-3 py-1 rounded bg-groove-accent hover:bg-blue-500 disabled:opacity-50 text-white text-sm font-semibold"
            title={transport.playing ? 'Grab the last bars of keys playing' : 'Press play first'}
          >
            ⏺ Grab Keys
          </button>
          {lenSelect(keyBars, setKeyBars, SRC_KEYS)}

          <button
            onClick={onUndo}
            disabled={!connected || ordered.length === 0}
            className="px-3 py-1 rounded bg-groove-border hover:bg-groove-yellow hover:text-black disabled:opacity-50 text-groove-text text-sm font-semibold"
            title="Delete the newest capture"
          >
            ↩ Undo
          </button>
        </div>
      </div>

      {/* Capture status flash */}
      {flashText && (
        <div
          className={`px-4 py-2 text-sm border-b border-groove-border ${
            captureFlash?.status === CAP_REFUSED ? 'text-groove-red' : 'text-groove-green'
          }`}
        >
          {flashText}
        </div>
      )}

      {/* Track rings */}
      <div className="p-4">
        {ordered.length === 0 ? (
          <p className="text-groove-muted italic text-sm">
            No loops yet. Play something, then hit Grab — the box has been
            listening the whole time.
          </p>
        ) : (
          <div className="flex flex-wrap gap-4">
            {ordered.map((t) => {
              const strip = strips[t.slot]
              const muted = strip?.mute ?? false
              return (
                <div
                  key={`${t.slot}-${t.gen}`}
                  className="flex flex-col items-center gap-1 p-2 rounded-lg border border-groove-border bg-groove-bg"
                >
                  <LoopRing
                    track={t}
                    muted={muted}
                    nowTick={nowTick}
                    playing={transport.playing}
                  />
                  <span className="text-xs text-groove-muted">
                    {KIND_NAMES[t.kind] ?? '?'} · {t.lengthBars} bar
                    {t.lengthBars > 1 ? 's' : ''}
                  </span>
                  <div className="flex items-center gap-1">
                    <button
                      onClick={() => onMute(t.slot, !muted)}
                      disabled={!connected}
                      className={`px-2 py-0.5 rounded text-xs font-bold transition-colors disabled:opacity-50 ${
                        muted
                          ? 'bg-groove-red text-white'
                          : 'bg-groove-border text-groove-muted hover:text-groove-text'
                      }`}
                    >
                      {muted ? 'MUTED' : 'MUTE'}
                    </button>
                    <HoldDeleteButton
                      onDelete={() => onDelete(t.slot, t.gen)}
                      disabled={!connected}
                    />
                  </div>
                </div>
              )
            })}
          </div>
        )}
      </div>
    </div>
  )
}
