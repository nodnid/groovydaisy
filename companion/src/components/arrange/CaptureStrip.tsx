import type { CaptureFlash, SrcActivity, PoolState } from '../../core/state'
import {
  SRC_PADS,
  SRC_KEYS,
  SRC_GUITAR,
  SOURCE_NAMES,
  CAP_PENDING,
  CAP_COMMITTED,
  CAP_REFUSED,
  ERROR_NAMES,
} from '../../core/protocol'

const BAR_CHOICES = [1, 2, 4, 8]
const MAX_BANKED = 8

interface CaptureStripProps {
  activity: SrcActivity
  captureFlash: CaptureFlash | null
  pool: PoolState
  tempoLocked: boolean
  padBars: number
  keyBars: number
  guitarBars: number
  onPadBars: (bars: number) => void
  onKeyBars: (bars: number) => void
  onGuitarBars: (bars: number) => void
  onCapture: (source: number, bars: number) => void
  onUndo: () => void
  playing: boolean
  hasTracks: boolean
  connected: boolean
}

/**
 * The always-listening engine made visible: per-source history rings fill
 * as the rolling buffer banks your playing ("4 bars ready to grab"), glow
 * while the source is being played, and the Grab button lifts the window
 * into a new track.
 */
function HistoryRing({
  banked,
  active,
  color,
  wanted,
}: {
  banked: number
  active: boolean
  color: string
  wanted: number
}) {
  const r = 14
  const c = 2 * Math.PI * r
  const frac = Math.min(banked / MAX_BANKED, 1)
  const enough = banked >= wanted
  return (
    <svg viewBox="0 0 36 36" className="w-9 h-9">
      <circle cx="18" cy="18" r={r} fill="none" stroke="#30363d" strokeWidth="4" />
      <circle
        cx="18"
        cy="18"
        r={r}
        fill="none"
        stroke={color}
        strokeWidth="4"
        strokeDasharray={`${frac * c} ${c}`}
        transform="rotate(-90 18 18)"
        strokeLinecap="round"
        opacity={enough ? 1 : 0.5}
      />
      {active && (
        <circle cx="18" cy="18" r={5} fill={color} opacity="0.9">
          <animate
            attributeName="opacity"
            values="0.9;0.3;0.9"
            dur="0.8s"
            repeatCount="indefinite"
          />
        </circle>
      )}
      <text
        x="18"
        y="21"
        textAnchor="middle"
        fontSize="9"
        fontWeight="bold"
        fill={active ? '#0d1117' : '#c9d1d9'}
      >
        {banked}
      </text>
    </svg>
  )
}

export default function CaptureStrip({
  activity,
  captureFlash,
  pool,
  tempoLocked,
  padBars,
  keyBars,
  guitarBars,
  onPadBars,
  onKeyBars,
  onGuitarBars,
  onCapture,
  onUndo,
  playing,
  hasTracks,
  connected,
}: CaptureStripProps) {
  const flashText = (() => {
    if (!captureFlash) return null
    const src = SOURCE_NAMES[captureFlash.source] ?? '?'
    switch (captureFlash.status) {
      case CAP_PENDING:
        return { text: `⏳ waiting for the bar line… (${src})`, bad: false }
      case CAP_COMMITTED:
        return {
          text: `✓ grabbed ${captureFlash.bars} bars of ${src} → track ${captureFlash.slot}`,
          bad: false,
        }
      case CAP_REFUSED:
        return {
          text: `✗ ${ERROR_NAMES[captureFlash.reason] ?? 'capture refused'}`,
          bad: true,
        }
      default:
        return null
    }
  })()

  const srcRow = (
    source: number,
    label: string,
    color: string,
    banked: number,
    active: boolean,
    bars: number,
    setBars: (b: number) => void
  ) => (
    <div className="flex items-center gap-2">
      <HistoryRing banked={banked} active={active} color={color} wanted={bars} />
      <button
        onClick={() => onCapture(source, bars)}
        disabled={!connected || !playing}
        className="px-3 py-1.5 rounded font-semibold text-sm text-white disabled:opacity-40"
        style={{ background: color }}
        title={
          !playing
            ? 'Press play first — the box only listens while the clock runs'
            : `Grab the last ${bars} bars of ${label.toLowerCase()}`
        }
      >
        ⏺ {label}
      </button>
      <select
        value={bars}
        disabled={!connected}
        onChange={(e) => setBars(parseInt(e.target.value))}
        className="bg-groove-bg border border-groove-border rounded text-groove-text text-xs px-1 py-1"
      >
        {BAR_CHOICES.map((b) => (
          <option key={b} value={b}>
            {b} bar{b > 1 ? 's' : ''}
          </option>
        ))}
      </select>
    </div>
  )

  return (
    <div className="bg-groove-panel border border-groove-border rounded-lg px-4 py-2">
      <div className="flex items-center gap-6 flex-wrap">
        {srcRow(SRC_PADS, 'Grab Pads', '#3fb950', activity.padsBarsBanked,
                activity.padsActive, padBars, onPadBars)}
        {srcRow(SRC_KEYS, 'Grab Keys', '#58a6ff', activity.keysBarsBanked,
                activity.keysActive, keyBars, onKeyBars)}
        {srcRow(SRC_GUITAR, 'Grab Guitar', '#d29922',
                activity.guitarBarsBanked, activity.guitarActive,
                guitarBars, onGuitarBars)}

        {/* Audio memory gauge: only meaningful once the pool is locked */}
        {pool.barsTotal > 0 && (
          <div
            className="flex flex-col items-center text-xs"
            title={
              tempoLocked
                ? 'Audio loop memory — tempo is locked while audio loops exist'
                : 'Audio loop memory'
            }
          >
            <span className="font-mono text-groove-text">
              {pool.barsFree}
              <span className="text-groove-muted">/{pool.barsTotal}</span>
            </span>
            <span className="text-groove-muted">bars free 🔒</span>
          </div>
        )}

        <div className="ml-auto flex items-center gap-3">
          {flashText && (
            <span
              className={`text-sm ${flashText.bad ? 'text-groove-red' : 'text-groove-green'}`}
            >
              {flashText.text}
            </span>
          )}
          <button
            onClick={onUndo}
            disabled={!connected || !hasTracks}
            className="px-3 py-1.5 rounded bg-groove-border hover:bg-groove-yellow hover:text-black disabled:opacity-40 text-groove-text text-sm font-semibold"
            title="Delete the newest capture"
          >
            ↩ Undo
          </button>
        </div>
      </div>
    </div>
  )
}
