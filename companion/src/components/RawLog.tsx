import { useEffect, useRef } from 'react'
import type { LogEntry } from '../App'

interface RawLogProps {
  logs: LogEntry[]
}

export default function RawLog({ logs }: RawLogProps) {
  const containerRef = useRef<HTMLDivElement>(null)

  // Auto-scroll to bottom when new logs arrive
  useEffect(() => {
    if (containerRef.current) {
      containerRef.current.scrollTop = containerRef.current.scrollHeight
    }
  }, [logs])

  return (
    <div
      ref={containerRef}
      className="bg-groove-bg border-t border-groove-border max-h-48 overflow-y-auto p-4 font-mono text-xs"
    >
      {logs.length === 0 ? (
        <p className="text-groove-muted italic">No messages yet. Connect to Daisy to see data.</p>
      ) : (
        <div className="space-y-1">
          {logs.map((log, i) => (
            <div key={i} className="flex gap-2">
              <span className="text-groove-muted opacity-50">
                {new Date(log.timestamp).toLocaleTimeString()}
              </span>
              <span
                className={
                  log.direction === '>'
                    ? 'text-groove-green'
                    : 'text-groove-accent'
                }
              >
                {log.direction}
              </span>
              <span className="text-groove-muted">{log.data}</span>
            </div>
          ))}
        </div>
      )}
    </div>
  )
}
