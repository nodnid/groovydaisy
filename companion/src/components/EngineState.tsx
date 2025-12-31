export interface TransportState {
  playing: boolean
  recording: boolean
  bpm: number
}

export interface Position {
  bar: number
  beat: number
  tick: number
}

interface EngineStateProps {
  tickCounter?: number
  checksumErrors?: number
  transport?: TransportState
  position?: Position
  synthVoices?: number
}

export default function EngineState({
  tickCounter = 0,
  checksumErrors = 0,
  transport = { playing: false, recording: false, bpm: 120 },
  position,
  synthVoices = 0,
}: EngineStateProps) {
  // Placeholder data for UI preview (will be replaced with real data in later steps)
  const cpuLoad = 0
  const pattern = 1

  // Use provided position or calculate from tick counter
  const PPQN = 96
  const ticksPerBar = PPQN * 4
  const bar = position?.bar ?? Math.floor(tickCounter / ticksPerBar) + 1
  const beat = position?.beat ?? Math.floor((tickCounter % ticksPerBar) / PPQN) + 1

  return (
    <div className="bg-groove-panel border border-groove-border rounded-lg">
      <div className="px-4 py-3 border-b border-groove-border">
        <h2 className="font-semibold text-groove-text">Engine State</h2>
      </div>
      <div className="p-4 space-y-4">
        {/* Voice Indicators - Synth only (drums shown via MIDI Monitor) */}
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
            <p className="text-2xl font-bold text-groove-text">{transport.bpm} BPM</p>
          </div>
          <div>
            <span className="text-groove-muted text-sm">Pattern</span>
            <p className="text-2xl font-bold text-groove-text">{pattern}/16</p>
          </div>
          <div className="col-span-2">
            <span className="text-groove-muted text-sm">Position</span>
            <p className="text-xl font-bold text-groove-text">
              Bar {bar}, Beat {beat}
              {transport.playing && (
                <span className="ml-2 text-sm text-groove-green">(playing)</span>
              )}
              {transport.recording && (
                <span className="ml-2 text-sm text-groove-red">(recording)</span>
              )}
            </p>
          </div>
        </div>

        {/* Protocol Stats */}
        <div className="pt-2 border-t border-groove-border">
          <div className="flex items-center justify-between text-sm">
            <span className="text-groove-muted">Tick Counter:</span>
            <span className="font-mono text-groove-accent">{tickCounter.toLocaleString()}</span>
          </div>
          {checksumErrors > 0 && (
            <div className="flex items-center justify-between text-sm mt-1">
              <span className="text-groove-muted">Checksum Errors:</span>
              <span className="font-mono text-groove-red">{checksumErrors}</span>
            </div>
          )}
        </div>
      </div>
    </div>
  )
}
