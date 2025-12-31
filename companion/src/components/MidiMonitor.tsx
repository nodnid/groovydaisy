import { useState, useEffect, useRef } from 'react'
import type { MidiMessageType } from '../core/midi-utils'

export interface MidiLogEntry {
  type: MidiMessageType
  channel: number
  data: string
  timestamp: number
}

interface MidiMonitorProps {
  messages: MidiLogEntry[]
  onClear?: () => void
}

type FilterType = 'All' | 'Notes' | 'CCs'

export default function MidiMonitor({ messages, onClear }: MidiMonitorProps) {
  const [filter, setFilter] = useState<FilterType>('All')
  const containerRef = useRef<HTMLDivElement>(null)

  // Auto-scroll to bottom when new messages arrive
  useEffect(() => {
    if (containerRef.current) {
      containerRef.current.scrollTop = containerRef.current.scrollHeight
    }
  }, [messages])

  // Filter messages based on selected filter
  const filteredMessages = messages.filter((msg) => {
    switch (filter) {
      case 'Notes':
        return msg.type === 'Note On' || msg.type === 'Note Off'
      case 'CCs':
        return msg.type === 'CC'
      default:
        return true
    }
  })

  // Get color for message type
  const getTypeColor = (type: MidiMessageType): string => {
    switch (type) {
      case 'Note On':
        return 'text-groove-green'
      case 'Note Off':
        return 'text-groove-red'
      case 'CC':
        return 'text-groove-yellow'
      case 'Pitch Bend':
        return 'text-groove-accent'
      default:
        return 'text-groove-muted'
    }
  }

  // Format timestamp as mm:ss.ms
  const formatTime = (timestamp: number): string => {
    const date = new Date(timestamp)
    const mins = date.getMinutes().toString().padStart(2, '0')
    const secs = date.getSeconds().toString().padStart(2, '0')
    const ms = date.getMilliseconds().toString().padStart(3, '0')
    return `${mins}:${secs}.${ms}`
  }

  return (
    <div className="bg-groove-panel border border-groove-border rounded-lg flex flex-col">
      <div className="px-4 py-3 border-b border-groove-border flex items-center justify-between">
        <h2 className="font-semibold text-groove-text">MIDI Input</h2>
        <div className="flex items-center gap-2">
          {/* Filter buttons */}
          <div className="flex gap-1 text-xs">
            {(['All', 'Notes', 'CCs'] as FilterType[]).map((f) => (
              <button
                key={f}
                onClick={() => setFilter(f)}
                className={`px-2 py-1 rounded transition-colors ${
                  filter === f
                    ? 'bg-groove-accent text-white'
                    : 'bg-groove-border text-groove-muted hover:text-groove-text'
                }`}
              >
                {f}
              </button>
            ))}
          </div>
          {/* Clear button */}
          {onClear && messages.length > 0 && (
            <button
              onClick={onClear}
              className="text-xs text-groove-muted hover:text-groove-text ml-2"
            >
              Clear
            </button>
          )}
        </div>
      </div>
      <div
        ref={containerRef}
        className="flex-1 p-4 overflow-y-auto max-h-96 font-mono text-sm"
      >
        {filteredMessages.length === 0 ? (
          <p className="text-groove-muted italic">
            {messages.length === 0
              ? 'No MIDI messages yet. Connect KeyLab and play!'
              : 'No messages match the current filter.'}
          </p>
        ) : (
          <div className="space-y-1">
            {filteredMessages.map((msg, i) => (
              <div key={i} className="flex gap-3">
                <span className="text-groove-muted opacity-50 w-20 flex-shrink-0">
                  {formatTime(msg.timestamp)}
                </span>
                <span className="text-groove-muted w-6 flex-shrink-0">
                  {msg.channel}
                </span>
                <span className={`w-20 flex-shrink-0 ${getTypeColor(msg.type)}`}>
                  {msg.type}
                </span>
                <span className="text-groove-text">{msg.data}</span>
              </div>
            ))}
          </div>
        )}
      </div>
      {/* Message count */}
      <div className="px-4 py-2 border-t border-groove-border text-xs text-groove-muted">
        {filteredMessages.length} / {messages.length} messages
      </div>
    </div>
  )
}
