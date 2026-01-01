/**
 * ResourceMeter - Displays memory usage and CPU load
 *
 * Shows visual bars for:
 * - Memory: Used MB / Total MB with percentage
 * - CPU: Load percentage
 */

interface ResourceMeterProps {
  memoryUsed: number   // bytes
  memoryTotal: number  // bytes
  cpuLoad: number      // 0-100
}

function formatBytes(bytes: number): string {
  if (bytes < 1024 * 1024) {
    return `${(bytes / 1024).toFixed(1)} KB`
  }
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`
}

function ProgressBar({ value, max, color }: { value: number; max: number; color: string }) {
  const percentage = Math.min(100, (value / max) * 100)

  return (
    <div className="h-3 bg-groove-border rounded-full overflow-hidden">
      <div
        className={`h-full transition-all duration-300 rounded-full ${color}`}
        style={{ width: `${percentage}%` }}
      />
    </div>
  )
}

export default function ResourceMeter({ memoryUsed, memoryTotal, cpuLoad }: ResourceMeterProps) {
  const memPct = memoryTotal > 0 ? (memoryUsed / memoryTotal) * 100 : 0
  const memColor = memPct > 80 ? 'bg-groove-red' : memPct > 60 ? 'bg-yellow-500' : 'bg-groove-green'
  const cpuColor = cpuLoad > 80 ? 'bg-groove-red' : cpuLoad > 60 ? 'bg-yellow-500' : 'bg-groove-green'

  return (
    <div className="flex gap-6 text-sm">
      {/* Memory */}
      <div className="flex items-center gap-3 flex-1">
        <span className="text-groove-text-dim font-medium w-16">Memory</span>
        <div className="flex-1 max-w-48">
          <ProgressBar value={memoryUsed} max={memoryTotal} color={memColor} />
        </div>
        <span className="text-groove-text-dim w-32 text-right">
          {formatBytes(memoryUsed)} / {formatBytes(memoryTotal)}
        </span>
        <span className="text-groove-text-dim w-12 text-right">
          {memPct.toFixed(0)}%
        </span>
      </div>

      {/* CPU */}
      <div className="flex items-center gap-3 flex-1">
        <span className="text-groove-text-dim font-medium w-12">CPU</span>
        <div className="flex-1 max-w-32">
          <ProgressBar value={cpuLoad} max={100} color={cpuColor} />
        </div>
        <span className="text-groove-text-dim w-12 text-right">
          {cpuLoad}%
        </span>
      </div>
    </div>
  )
}
