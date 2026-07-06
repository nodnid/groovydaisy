import type { TransportState, StatsState } from '../core/state'

interface EngineStateProps {
  checksumErrors?: number
  transport: TransportState
  synthVoices?: number
  drumVoices?: number
  protoVer: number | null
  cpuPct: number
  stats: StatsState
}

export default function EngineState({
  checksumErrors = 0,
  transport,
  synthVoices = 0,
  drumVoices = 0,
  protoVer,
  cpuPct,
  stats,
}: EngineStateProps) {
  const cpuLoad = cpuPct
  const anyDrops =
    stats.midiDrops + stats.ccDrops + stats.txLappedInt + stats.txLappedExt > 0

  return (
    <div className="bg-groove-panel border border-groove-border rounded-lg">
      <div className="px-4 py-3 border-b border-groove-border flex items-center justify-between">
        <h2 className="font-semibold text-groove-text">Engine State</h2>
        <span className="text-xs text-groove-muted font-mono">
          {protoVer !== null ? `proto v${protoVer}` : 'no hello yet'}
        </span>
      </div>
      <div className="p-4 space-y-4">
        {/* Voice Indicators */}
        <div className="space-y-2">
          <div className="flex items-center justify-between">
            <span className="text-groove-muted">Synth Voices:</span>
            <div className="flex items-center gap-1">
              {Array.from({ length: 6 }).map((_, i) => (
                <div
                  key={i}
                  className={`w-4 h-4 rounded-full ${
                    i < synthVoices ? 'bg-groove-accent' : 'bg-groove-border'
                  }`}
                />
              ))}
              <span className="ml-2 text-groove-muted">({synthVoices}/6)</span>
            </div>
          </div>
          <div className="flex items-center justify-between">
            <span className="text-groove-muted">Drum Voices:</span>
            <div className="flex items-center gap-1">
              {Array.from({ length: 8 }).map((_, i) => (
                <div
                  key={i}
                  className={`w-3 h-3 rounded-full ${
                    i < drumVoices ? 'bg-groove-green' : 'bg-groove-border'
                  }`}
                />
              ))}
              <span className="ml-2 text-groove-muted">({drumVoices}/8)</span>
            </div>
          </div>
        </div>

        {/* CPU Load */}
        <div className="space-y-1">
          <div className="flex items-center justify-between">
            <span className="text-groove-muted">CPU Load:</span>
            <span className="text-groove-text">{cpuLoad}%</span>
          </div>
          <div className="h-2 bg-groove-border rounded-full overflow-hidden">
            <div
              className={`h-full transition-all ${
                cpuLoad > 80
                  ? 'bg-groove-red'
                  : cpuLoad > 60
                  ? 'bg-groove-yellow'
                  : 'bg-groove-green'
              }`}
              style={{ width: `${cpuLoad}%` }}
            />
          </div>
        </div>

        {/* Transport Info */}
        <div className="grid grid-cols-2 gap-4 pt-2 border-t border-groove-border">
          <div>
            <span className="text-groove-muted text-sm">Tempo</span>
            <p className="text-2xl font-bold text-groove-text">
              {Number.isInteger(transport.bpm)
                ? transport.bpm
                : transport.bpm.toFixed(1)}{' '}
              BPM
              {transport.tempoLocked && (
                <span className="ml-1 text-sm" title="Tempo locked (audio loops exist)">
                  🔒
                </span>
              )}
            </p>
          </div>
          <div>
            <span className="text-groove-muted text-sm">Transport</span>
            <p
              className={`text-2xl font-bold ${
                transport.playing ? 'text-groove-green' : 'text-groove-muted'
              }`}
            >
              {transport.playing ? 'PLAYING' : 'STOPPED'}
            </p>
          </div>
        </div>

        {/* Protocol Stats */}
        {checksumErrors > 0 && (
          <div className="pt-2 border-t border-groove-border">
            <div className="flex items-center justify-between text-sm">
              <span className="text-groove-muted">Checksum Errors:</span>
              <span className="font-mono text-groove-red">{checksumErrors}</span>
            </div>
          </div>
        )}

        {/* Ring-drop diagnostics: silent when healthy (Phase 6) */}
        {anyDrops && (
          <div className="pt-2 border-t border-groove-border space-y-1 text-sm">
            <span className="text-groove-yellow font-semibold">Ring drops detected</span>
            {stats.midiDrops > 0 && (
              <div className="flex justify-between">
                <span className="text-groove-muted">MIDI → audio:</span>
                <span className="font-mono text-groove-red">{stats.midiDrops}</span>
              </div>
            )}
            {stats.ccDrops > 0 && (
              <div className="flex justify-between">
                <span className="text-groove-muted">CC playback:</span>
                <span className="font-mono text-groove-red">{stats.ccDrops}</span>
              </div>
            )}
            {(stats.txLappedInt > 0 || stats.txLappedExt > 0) && (
              <div className="flex justify-between">
                <span className="text-groove-muted">TX lapped (int/ext):</span>
                <span className="font-mono text-groove-yellow">
                  {stats.txLappedInt} / {stats.txLappedExt}
                </span>
              </div>
            )}
          </div>
        )}
      </div>
    </div>
  )
}
