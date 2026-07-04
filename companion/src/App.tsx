import { useState, useRef, useCallback, useReducer, useEffect } from 'react'
import ConnectionStatus from './components/ConnectionStatus'
import TransportBar from './components/TransportBar'
import MidiMonitor, { type MidiLogEntry } from './components/MidiMonitor'
import EngineState from './components/EngineState'
import LiveMixer from './components/LiveMixer'
import TrackList from './components/TrackList'
import RawLog from './components/RawLog'
import SynthPanel from './components/SynthPanel'
import PresetManager from './components/PresetManager'
import CCControlPanel from './components/CCControlPanel'
import { WebSerialPort, bytesToHex } from './serial/WebSerialPort'
import { Bank } from './core/ccMappings'
import { STRIP_MASTER } from './core/constants'
import {
  ProtocolParser,
  ParsedMessage,
  SynthParams,
  SynthParamId,
  MSG_DEBUG,
  MSG_MIDI_IN,
  MSG_ERROR,
  ERROR_NAMES,
  MIX_FIELD_LEVEL,
  MIX_FIELD_PAN,
  MIX_FIELD_MUTE,
  buildMessage,
  buildTempoCommand,
  buildSynthParamCommand,
  buildLoadPresetCommand,
  buildSetBankCommand,
  buildMixerCommand,
  buildMetroCommand,
  buildMonitorCommand,
  buildCaptureCommand,
  buildUndoCommand,
  buildTrackDeleteCommand,
  buildSrcLenCommand,
  CMD_PLAY,
  CMD_STOP,
  CMD_REWIND,
  CMD_TAP,
  CMD_REQ_STATE,
} from './core/protocol'
import { deviceReducer, getInitialDeviceState } from './core/state'
import { parseMidiMessage } from './core/midi-utils'

export interface LogEntry {
  direction: '>' | '<'
  data: string
  timestamp: number
}

export interface DebugEntry {
  text: string
  timestamp: number
}

const MAX_LOG_ENTRIES = 200
const MAX_DEBUG_ENTRIES = 50
const MAX_MIDI_ENTRIES = 200

function App() {
  // Device state: pure reducer over protocol messages (core/state.ts)
  const [device, dispatch] = useReducer(deviceReducer, undefined, getInitialDeviceState)

  // UI-only state (logs, connection, toggles)
  const [connected, setConnected] = useState(false)
  const [showRawLog, setShowRawLog] = useState(false)
  const [logs, setLogs] = useState<LogEntry[]>([])
  const [debugMessages, setDebugMessages] = useState<DebugEntry[]>([])
  const [midiMessages, setMidiMessages] = useState<MidiLogEntry[]>([])
  const [checksumErrors, setChecksumErrors] = useState<number>(0)
  const [monitorEnabled, setMonitorEnabled] = useState(false)
  const serialRef = useRef<WebSerialPort | null>(null)
  const parserRef = useRef<ProtocolParser | null>(null)

  // Auto-dismiss device errors after a few seconds
  useEffect(() => {
    if (device.lastError) {
      const t = setTimeout(() => dispatch({ kind: 'clearError' }), 4000)
      return () => clearTimeout(t)
    }
  }, [device.lastError])

  // Capture flashes fade too (pending flashes stay until resolved)
  useEffect(() => {
    if (device.captureFlash && device.captureFlash.status !== 0 /* pending */) {
      const t = setTimeout(() => dispatch({ kind: 'clearCaptureFlash' }), 4000)
      return () => clearTimeout(t)
    }
  }, [device.captureFlash])

  const addLog = useCallback((direction: '>' | '<', data: string) => {
    setLogs((prev) => {
      const newLogs = [...prev, { direction, data, timestamp: Date.now() }]
      if (newLogs.length > MAX_LOG_ENTRIES) {
        return newLogs.slice(-MAX_LOG_ENTRIES)
      }
      return newLogs
    })
  }, [])

  const handleMessage = useCallback(
    (msg: ParsedMessage) => {
      // All device state flows through the reducer...
      dispatch({ kind: 'message', msg, nowMs: performance.now() })

      // ...log-type side effects handled here
      switch (msg.type) {
        case MSG_DEBUG:
          setDebugMessages((prev) => {
            const next = [...prev, { text: msg.text, timestamp: Date.now() }]
            return next.length > MAX_DEBUG_ENTRIES ? next.slice(-MAX_DEBUG_ENTRIES) : next
          })
          addLog('>', `DEBUG: ${msg.text}`)
          break
        case MSG_MIDI_IN: {
          const parsed = parseMidiMessage(msg.status, msg.data1, msg.data2)
          setMidiMessages((prev) => {
            const next: MidiLogEntry[] = [
              ...prev,
              {
                type: parsed.type,
                channel: parsed.channel,
                data: parsed.data,
                timestamp: Date.now(),
              },
            ]
            return next.length > MAX_MIDI_ENTRIES ? next.slice(-MAX_MIDI_ENTRIES) : next
          })
          break
        }
        case MSG_ERROR:
          addLog('>', `ERROR: ${ERROR_NAMES[msg.code] ?? `code ${msg.code}`}`)
          break
        default:
          break
      }
    },
    [addLog]
  )

  const handleChecksumError = useCallback(() => {
    setChecksumErrors((prev) => prev + 1)
  }, [])

  const send = useCallback((bytes: Uint8Array) => {
    serialRef.current?.send(bytes)
  }, [])

  // --- command handlers ---
  const handlePlay = useCallback(() => send(buildMessage(CMD_PLAY)), [send])
  const handleStop = useCallback(() => send(buildMessage(CMD_STOP)), [send])
  const handleRewind = useCallback(() => send(buildMessage(CMD_REWIND)), [send])
  const handleTap = useCallback(() => send(buildMessage(CMD_TAP)), [send])
  const handleTempoChange = useCallback(
    (bpm: number) => send(buildTempoCommand(bpm)),
    [send]
  )
  const handleSynthParamChange = useCallback(
    (paramId: SynthParamId, value: number) => send(buildSynthParamCommand(paramId, value)),
    [send]
  )
  const handleLoadFactoryPreset = useCallback(
    (index: number) => send(buildLoadPresetCommand(index)),
    [send]
  )
  const handleBankChange = useCallback(
    (bank: Bank) => send(buildSetBankCommand(bank)),
    [send]
  )
  const handleStripChange = useCallback(
    (strip: number, field: 'level' | 'pan' | 'mute', value: number) => {
      const fieldId =
        field === 'level' ? MIX_FIELD_LEVEL : field === 'pan' ? MIX_FIELD_PAN : MIX_FIELD_MUTE
      send(buildMixerCommand(strip, fieldId, value))
    },
    [send]
  )
  const handleMasterChange = useCallback(
    (value: number) => send(buildMixerCommand(STRIP_MASTER, MIX_FIELD_LEVEL, value)),
    [send]
  )
  const handleMetroToggle = useCallback(
    (on: boolean) => send(buildMetroCommand(on, device.metro.level)),
    [send, device.metro.level]
  )
  const handleToggleMonitor = useCallback(
    (on: boolean) => {
      setMonitorEnabled(on)
      send(buildMonitorCommand(on))
    },
    [send]
  )
  const handleCapture = useCallback(
    (source: number, bars: number) => send(buildCaptureCommand(source, bars)),
    [send]
  )
  const handleUndo = useCallback(() => send(buildUndoCommand()), [send])
  const handleTrackDelete = useCallback(
    (slot: number, gen: number) => send(buildTrackDeleteCommand(slot, gen)),
    [send]
  )
  const handleTrackMute = useCallback(
    (slot: number, mute: boolean) =>
      send(buildMixerCommand(slot, MIX_FIELD_MUTE, mute ? 1 : 0)),
    [send]
  )
  const handleSrcLen = useCallback(
    (source: number, bars: number) => send(buildSrcLenCommand(source, bars)),
    [send]
  )

  const handleLoadUserPreset = useCallback(
    (params: SynthParams) => {
      const paramMap: [SynthParamId, number][] = [
        [SynthParamId.OSC1_WAVE, params.osc1Wave],
        [SynthParamId.OSC2_WAVE, params.osc2Wave],
        [SynthParamId.OSC1_LEVEL, params.osc1Level],
        [SynthParamId.OSC2_LEVEL, params.osc2Level],
        [SynthParamId.OSC2_DETUNE, params.osc2Detune],
        [SynthParamId.FILTER_CUTOFF, params.filterCutoff],
        [SynthParamId.FILTER_RES, params.filterRes],
        [SynthParamId.FILTER_ENV_AMT, params.filterEnvAmt],
        [SynthParamId.AMP_ATTACK, params.ampAttack],
        [SynthParamId.AMP_DECAY, params.ampDecay],
        [SynthParamId.AMP_SUSTAIN, params.ampSustain],
        [SynthParamId.AMP_RELEASE, params.ampRelease],
        [SynthParamId.FILT_ATTACK, params.filtAttack],
        [SynthParamId.FILT_DECAY, params.filtDecay],
        [SynthParamId.FILT_SUSTAIN, params.filtSustain],
        [SynthParamId.FILT_RELEASE, params.filtRelease],
        [SynthParamId.VEL_TO_AMP, params.velToAmp],
        [SynthParamId.VEL_TO_FILTER, params.velToFilter],
        [SynthParamId.LEVEL, params.level],
      ]
      for (const [paramId, value] of paramMap) {
        send(buildSynthParamCommand(paramId, value))
      }
    },
    [send]
  )

  const handleConnect = async () => {
    if (connected && serialRef.current) {
      await serialRef.current.disconnect()
      parserRef.current = null
      return
    }

    if (!WebSerialPort.isSupported()) {
      alert('WebSerial is not supported in this browser. Please use Chrome or Edge.')
      return
    }

    const parser = new ProtocolParser(handleMessage, handleChecksumError)
    parserRef.current = parser

    const serial = new WebSerialPort({
      onData: (data) => {
        parser.feed(data)
        if (showRawLog) {
          addLog('>', bytesToHex(data))
        }
      },
      onConnect: () => {
        setConnected(true)
        setChecksumErrors(0)
        setMidiMessages([])
        setMonitorEnabled(false)
        dispatch({ kind: 'reset' })
        addLog('<', '-- Connected --')
        // One snapshot request hydrates everything (v2 cold-join)
        setTimeout(() => {
          serial.send(buildMessage(CMD_REQ_STATE))
        }, 100)
      },
      onDisconnect: () => {
        setConnected(false)
        parserRef.current?.reset()
        addLog('<', '-- Disconnected --')
      },
      onError: (error) => {
        console.error('Serial error:', error)
        addLog('<', `ERROR: ${error.message}`)
      },
    })

    serialRef.current = serial
    await serial.connect()
  }

  return (
    <div className="min-h-screen bg-groove-bg flex flex-col">
      {/* Header */}
      <header className="bg-groove-panel border-b border-groove-border px-4 py-3 flex items-center justify-between">
        <div className="flex items-center gap-4">
          <h1 className="text-xl font-bold text-groove-text">GroovyDaisy</h1>
          <ConnectionStatus connected={connected} />
        </div>
        <div className="flex items-center gap-3">
          {device.lastError && (
            <span className="px-3 py-1 rounded bg-groove-red text-white text-sm">
              {ERROR_NAMES[device.lastError.code] ?? `Error ${device.lastError.code}`}
            </span>
          )}
          <button
            onClick={handleConnect}
            className={`px-4 py-2 rounded font-medium transition-colors ${
              connected
                ? 'bg-groove-red hover:bg-red-600 text-white'
                : 'bg-groove-accent hover:bg-blue-500 text-white'
            }`}
          >
            {connected ? 'Disconnect' : 'Connect'}
          </button>
        </div>
      </header>

      {/* Transport Bar */}
      <div className="px-4 pt-4">
        <TransportBar
          transport={device.transport}
          sync={device.sync}
          onPlay={handlePlay}
          onStop={handleStop}
          onRewind={handleRewind}
          onTap={handleTap}
          onTempoChange={handleTempoChange}
          connected={connected}
        />
      </div>

      {/* Main Content */}
      <main className="flex-1 p-4 space-y-4 overflow-y-auto">
        {/* Loop Tracks */}
        <TrackList
          tracks={device.tracks}
          strips={device.strips}
          transport={device.transport}
          sync={device.sync}
          captureFlash={device.captureFlash}
          onCapture={handleCapture}
          onUndo={handleUndo}
          onDelete={handleTrackDelete}
          onMute={handleTrackMute}
          onSrcLen={handleSrcLen}
          connected={connected}
        />

        {/* Live Mixer */}
        <LiveMixer
          strips={device.strips}
          masterOut={device.engineMix.masterOut}
          metroOn={device.metro.on}
          onStripChange={handleStripChange}
          onMasterChange={handleMasterChange}
          onMetroToggle={handleMetroToggle}
          connected={connected}
        />

        {/* Top Row - MIDI Monitor, Engine State, Presets */}
        <div className="grid grid-cols-1 lg:grid-cols-3 gap-4">
          <MidiMonitor
            messages={midiMessages}
            onClear={() => setMidiMessages([])}
            monitorEnabled={monitorEnabled}
            onToggleMonitor={handleToggleMonitor}
            connected={connected}
          />
          <EngineState
            checksumErrors={checksumErrors}
            transport={device.transport}
            synthVoices={device.voices.synth}
            drumVoices={device.voices.drums}
            protoVer={device.protoVer}
          />
          <PresetManager
            currentPresetIndex={device.presetIndex}
            currentParams={device.synthParams}
            onLoadFactoryPreset={handleLoadFactoryPreset}
            onLoadUserPreset={handleLoadUserPreset}
            connected={connected}
          />
        </div>

        {/* CC Control Panel */}
        <CCControlPanel
          currentBank={device.bank as Bank}
          faderStates={device.faderStates}
          mixerState={device.engineMix}
          synthParams={device.synthParams}
          onBankChange={handleBankChange}
          connected={connected}
        />

        {/* Synth Panel */}
        <SynthPanel
          params={device.synthParams}
          onParamChange={handleSynthParamChange}
          connected={connected}
        />

        {/* Debug Messages */}
        {debugMessages.length > 0 && (
          <div className="bg-groove-panel border border-groove-border rounded-lg">
            <div className="px-4 py-3 border-b border-groove-border flex items-center justify-between">
              <h2 className="font-semibold text-groove-text">Debug Messages</h2>
              <button
                onClick={() => setDebugMessages([])}
                className="text-xs text-groove-muted hover:text-groove-text"
              >
                Clear
              </button>
            </div>
            <div className="p-4 max-h-48 overflow-y-auto font-mono text-xs space-y-1">
              {debugMessages.map((msg, i) => (
                <div key={i} className="flex gap-2">
                  <span className="text-groove-muted opacity-50">
                    {new Date(msg.timestamp).toLocaleTimeString()}
                  </span>
                  <span className="text-groove-yellow">{msg.text}</span>
                </div>
              ))}
            </div>
          </div>
        )}
      </main>

      {/* Footer - Raw Log Toggle */}
      <footer className="border-t border-groove-border">
        <button
          onClick={() => setShowRawLog(!showRawLog)}
          className="w-full px-4 py-2 text-left text-groove-muted hover:text-groove-text hover:bg-groove-panel transition-colors flex items-center gap-2"
        >
          <span className={`transform transition-transform ${showRawLog ? 'rotate-90' : ''}`}>
            ▶
          </span>
          Raw Log
        </button>
        {showRawLog && <RawLog logs={logs} />}
      </footer>
    </div>
  )
}

export default App
