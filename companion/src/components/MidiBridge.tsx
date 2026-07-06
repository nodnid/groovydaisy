import { useEffect, useRef, useState } from 'react'

interface MidiBridgeProps {
  /** Forward one MIDI event to the box (status, d1, d2). */
  onEvent: (status: number, d1: number, d2: number) => void
  connected: boolean
}

/**
 * Web MIDI -> GroovyDaisy bridge: any MIDI source on this machine (IAC
 * bus, a DAW, a script, a second controller) becomes a player. Events
 * ride the serial link via CMD_MIDI_INJECT and take the same path as the
 * KeyLab's — including the capture rings, so bridged playing is
 * grab-able like everything else.
 */
export default function MidiBridge({ onEvent, connected }: MidiBridgeProps) {
  const [supported] = useState(() => 'requestMIDIAccess' in navigator)
  const [access, setAccess] = useState<MIDIAccess | null>(null)
  const [inputs, setInputs] = useState<MIDIInput[]>([])
  const [enabled, setEnabled] = useState<Record<string, boolean>>({})
  const [eventCount, setEventCount] = useState(0)
  const onEventRef = useRef(onEvent)
  onEventRef.current = onEvent
  const enabledRef = useRef(enabled)
  enabledRef.current = enabled
  const connectedRef = useRef(connected)
  connectedRef.current = connected

  const requestAccess = async () => {
    try {
      const a = await navigator.requestMIDIAccess()
      setAccess(a)
    } catch (e) {
      console.error('Web MIDI access denied', e)
    }
  }

  // Track the input list (hot-plug aware)
  useEffect(() => {
    if (!access) return
    const refresh = () => setInputs(Array.from(access.inputs.values()))
    refresh()
    access.onstatechange = refresh
    return () => {
      access.onstatechange = null
    }
  }, [access])

  // Attach listeners to enabled inputs
  useEffect(() => {
    const handler = (input: MIDIInput) => (e: MIDIMessageEvent) => {
      if (!connectedRef.current || !enabledRef.current[input.id]) return
      const data = e.data
      if (!data || data.length < 1) return
      const type = data[0] & 0xf0
      // Notes and CCs only — same vocabulary the DIN input speaks
      if (type === 0x90 || type === 0x80 || type === 0xb0) {
        onEventRef.current(data[0], data[1] ?? 0, data[2] ?? 0)
        setEventCount((n) => n + 1)
      }
    }
    const detach: Array<() => void> = []
    for (const input of inputs) {
      const h = handler(input)
      input.addEventListener('midimessage', h as EventListener)
      detach.push(() =>
        input.removeEventListener('midimessage', h as EventListener)
      )
    }
    return () => detach.forEach((d) => d())
  }, [inputs])

  if (!supported) {
    return (
      <div className="bg-groove-panel border border-groove-border rounded-lg p-4 text-sm text-groove-muted">
        Web MIDI isn't supported in this browser.
      </div>
    )
  }

  return (
    <div className="bg-groove-panel border border-groove-border rounded-lg">
      <div className="px-4 py-3 border-b border-groove-border flex items-center justify-between">
        <h2 className="font-semibold text-groove-text">
          MIDI Bridge{' '}
          <span className="text-xs text-groove-muted font-normal">
            play the box from this Mac
          </span>
        </h2>
        {access ? (
          <span className="text-xs font-mono text-groove-muted">
            {eventCount} events forwarded
          </span>
        ) : (
          <button
            onClick={requestAccess}
            className="px-3 py-1 rounded bg-groove-accent hover:bg-blue-500 text-white text-xs font-semibold"
          >
            Enable Web MIDI
          </button>
        )}
      </div>
      {access && (
        <div className="p-4 space-y-2">
          {inputs.length === 0 ? (
            <p className="text-sm text-groove-muted italic">
              No MIDI inputs on this Mac. Tip: enable the IAC Driver in
              Audio MIDI Setup and any DAW or script can play the box.
            </p>
          ) : (
            inputs.map((input) => (
              <label
                key={input.id}
                className="flex items-center gap-2 text-sm text-groove-text cursor-pointer"
              >
                <input
                  type="checkbox"
                  checked={enabled[input.id] ?? false}
                  onChange={(e) =>
                    setEnabled((prev) => ({
                      ...prev,
                      [input.id]: e.target.checked,
                    }))
                  }
                  className="accent-blue-500"
                />
                <span>{input.name ?? input.id}</span>
                {input.manufacturer && (
                  <span className="text-groove-muted text-xs">
                    {input.manufacturer}
                  </span>
                )}
              </label>
            ))
          )}
        </div>
      )}
    </div>
  )
}
