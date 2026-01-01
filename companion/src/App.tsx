import { useState, useRef, useCallback } from 'react'
import ConnectionStatus from './components/ConnectionStatus'
import TransportBar, { type TransportState } from './components/TransportBar'
import MidiMonitor, { type MidiLogEntry } from './components/MidiMonitor'
import EngineState from './components/EngineState'
import RawLog from './components/RawLog'
import SynthPanel from './components/SynthPanel'
import PresetManager from './components/PresetManager'
import CCControlPanel, { type FaderPickupState } from './components/CCControlPanel'
import { WebSerialPort, bytesToHex } from './serial/WebSerialPort'
import { Bank, MixerState, getDefaultMixerState, ENCODER_CCS, FADER_CCS, getBankMappings, ParamTarget } from './core/ccMappings'
import {
  ProtocolParser,
  ParsedMessage,
  SynthParams,
  SynthParamId,
  PatternEvent,
  TrackStatus,
  MSG_TICK,
  MSG_DEBUG,
  MSG_TRANSPORT,
  MSG_VOICES,
  MSG_MIDI_IN,
  MSG_SYNTH_STATE,
  MSG_CC_BANK,
  MSG_FADER_STATE,
  MSG_MIXER_STATE,
  MSG_TRACK_STATE,
  MSG_PATTERN_DUMP,
  MSG_PATTERN_CLEAR,
  MSG_RESOURCES,
  getMessageTypeName,
  buildSetBankCommand,
  buildMessage,
  buildSynthParamCommand,
  buildLoadPresetCommand,
  buildRequestSynthCommand,
  buildFreezeTrackCommand,
  buildUnfreezeTrackCommand,
  getDefaultSynthParams,
  CMD_PLAY,
  CMD_STOP,
  CMD_RECORD,
  CMD_TEMPO,
  CMD_REQ_STATE,
} from './core/protocol'
import TabBar, { type TabId } from './components/global/TabBar'
import ArrangeView from './components/arrange/ArrangeView'
import { parseMidiMessage } from './core/midi-utils'

// CC value converters (CC 0-127 to parameter value)
function ccToNorm(cc: number): number { return cc / 127 }
function ccToFreq(cc: number): number { return 20 * Math.pow(1000, cc / 127) }
function ccToTime(cc: number): number { return 0.001 * Math.pow(5000, cc / 127) }
function ccToWave(cc: number): number { return Math.floor((cc * 4) / 128) }
function ccToSemi(cc: number): number { return Math.round(((cc - 64) * 24) / 64) }

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

// PPQN constants for position calculation
const PPQN = 96
const TICKS_PER_BAR = PPQN * 4  // 384 ticks per bar
const PATTERN_BARS = 4
const PATTERN_TICKS = TICKS_PER_BAR * PATTERN_BARS  // 1536 ticks per pattern

function App() {
  const [connected, setConnected] = useState(false)
  const [showRawLog, setShowRawLog] = useState(false)
  const [logs, setLogs] = useState<LogEntry[]>([])
  const [tickCounter, setTickCounter] = useState<number>(0)
  const [debugMessages, setDebugMessages] = useState<DebugEntry[]>([])
  const [midiMessages, setMidiMessages] = useState<MidiLogEntry[]>([])
  const [checksumErrors, setChecksumErrors] = useState<number>(0)
  const [transport, setTransport] = useState<TransportState>({
    playing: false,
    recording: false,
    bpm: 120,
  })
  const [voiceState, setVoiceState] = useState({ synth: 0, drums: 0 })
  const [synthParams, setSynthParams] = useState<SynthParams>(getDefaultSynthParams())
  const [presetIndex, setPresetIndex] = useState(0)
  const [currentBank, setCurrentBank] = useState<Bank>(Bank.SYNTH)
  const [faderStates, setFaderStates] = useState<FaderPickupState[]>(
    Array(9).fill({ pickedUp: true, needsPickup: false })
  )
  const [mixerState, setMixerState] = useState<MixerState>(getDefaultMixerState())

  // Tab navigation
  const [activeTab, setActiveTab] = useState<TabId>('arrange')

  // Resources (memory + CPU)
  const [resources, setResources] = useState({ memoryUsed: 0, memoryTotal: 64 * 1024 * 1024, cpuLoad: 0 })

  // Track states for synth tracks (index 8-11 in sequencer)
  const [synthTrackStates, setSynthTrackStates] = useState<Array<{ status: TrackStatus; frozenSlot: number }>>(
    Array(4).fill({ status: TrackStatus.MIDI, frozenSlot: 0xff })
  )

  // Pattern data: 12 tracks (8 drums + 4 synth)
  const [patternData, setPatternData] = useState<PatternEvent[][]>(
    Array(12).fill([]).map(() => [])
  )

  // Pending events: real-time MIDI events during recording (not yet synced from Daisy)
  // These are displayed with different styling until confirmed by pattern dump
  const [pendingEvents, setPendingEvents] = useState<PatternEvent[][]>(
    Array(12).fill([]).map(() => [])
  )

  const serialRef = useRef<WebSerialPort | null>(null)
  const parserRef = useRef<ProtocolParser | null>(null)
  const currentBankRef = useRef<Bank>(currentBank)
  currentBankRef.current = currentBank  // Keep ref in sync with state
  const transportRef = useRef<TransportState>(transport)
  transportRef.current = transport  // Keep ref in sync with state
  const tickRef = useRef<number>(tickCounter)
  tickRef.current = tickCounter  // Keep ref in sync with state

  // Calculate position from tick counter (with pattern wrapping)
  // The tick from Daisy is already wrapped, but handle it defensively
  const tickInPattern = tickCounter % PATTERN_TICKS
  const position = {
    bar: Math.floor(tickInPattern / TICKS_PER_BAR) + 1,
    beat: Math.floor((tickInPattern % TICKS_PER_BAR) / PPQN) + 1,
    tick: tickInPattern,
  }

  const addLog = useCallback((direction: '>' | '<', data: string) => {
    setLogs((prev) => {
      const newLogs = [...prev, { direction, data, timestamp: Date.now() }]
      // Keep only the last MAX_LOG_ENTRIES
      if (newLogs.length > MAX_LOG_ENTRIES) {
        return newLogs.slice(-MAX_LOG_ENTRIES)
      }
      return newLogs
    })
  }, [])

  const handleMessage = useCallback((msg: ParsedMessage) => {
    switch (msg.type) {
      case MSG_TICK:
        setTickCounter(msg.tick)
        break
      case MSG_DEBUG:
        setDebugMessages((prev) => {
          const newMsgs = [...prev, { text: msg.text, timestamp: Date.now() }]
          if (newMsgs.length > MAX_DEBUG_ENTRIES) {
            return newMsgs.slice(-MAX_DEBUG_ENTRIES)
          }
          return newMsgs
        })
        addLog('>', `DEBUG: ${msg.text}`)
        break
      case MSG_MIDI_IN:
        {
          const parsed = parseMidiMessage(msg.status, msg.data1, msg.data2)
          setMidiMessages((prev) => {
            const newMsgs: MidiLogEntry[] = [
              ...prev,
              {
                type: parsed.type,
                channel: parsed.channel,
                data: parsed.data,
                timestamp: Date.now(),
              },
            ]
            if (newMsgs.length > MAX_MIDI_ENTRIES) {
              return newMsgs.slice(-MAX_MIDI_ENTRIES)
            }
            return newMsgs
          })

          // Track pending events for real-time display during recording
          if (transportRef.current.recording) {
            const channel = msg.status & 0x0F
            const type = msg.status & 0xF0
            const tick = tickRef.current % PATTERN_TICKS

            // Drum events (channel 10, NoteOn with velocity > 0)
            if (channel === 9 && type === 0x90 && msg.data2 > 0) {
              const note = msg.data1
              if (note >= 36 && note <= 43) {
                const trackIdx = note - 36  // Drum tracks 0-7
                setPendingEvents(prev => {
                  const next = [...prev]
                  next[trackIdx] = [...prev[trackIdx], {
                    tick,
                    status: msg.status,
                    data1: msg.data1,
                    data2: msg.data2,
                  }]
                  return next
                })
              }
            }

            // Synth events (channel 1, NoteOn or NoteOff)
            if (channel === 0 && (type === 0x90 || type === 0x80)) {
              const note = msg.data1
              const trackIdx = 8 + (note % 4)  // Synth tracks 8-11
              setPendingEvents(prev => {
                const next = [...prev]
                next[trackIdx] = [...prev[trackIdx], {
                  tick,
                  status: msg.status,
                  data1: msg.data1,
                  data2: msg.data2,
                }]
                return next
              })
            }
          }

          // Update UI state from CC values
          const isCC = (msg.status & 0xF0) === 0xB0
          if (isCC) {
            const ccNum = msg.data1
            const ccVal = msg.data2

            // Find which encoder/fader this CC maps to
            const encIdx = ENCODER_CCS.indexOf(ccNum as typeof ENCODER_CCS[number])
            const fadIdx = FADER_CCS.indexOf(ccNum as typeof FADER_CCS[number])

            if (encIdx >= 0 || fadIdx >= 0) {
              const bankMappings = getBankMappings(currentBankRef.current)
              const mapping = encIdx >= 0
                ? bankMappings.encoders[encIdx]
                : bankMappings.faders[fadIdx]

              if (mapping && mapping.target !== ParamTarget.NONE) {
                // Update mixer state for drum/mixer params
                const target = mapping.target
                setMixerState(prev => {
                  const next = { ...prev }
                  switch (target) {
                    case ParamTarget.DRUM_1_LEVEL: next.drumLevels = [...prev.drumLevels]; next.drumLevels[0] = ccVal; break
                    case ParamTarget.DRUM_2_LEVEL: next.drumLevels = [...prev.drumLevels]; next.drumLevels[1] = ccVal; break
                    case ParamTarget.DRUM_3_LEVEL: next.drumLevels = [...prev.drumLevels]; next.drumLevels[2] = ccVal; break
                    case ParamTarget.DRUM_4_LEVEL: next.drumLevels = [...prev.drumLevels]; next.drumLevels[3] = ccVal; break
                    case ParamTarget.DRUM_5_LEVEL: next.drumLevels = [...prev.drumLevels]; next.drumLevels[4] = ccVal; break
                    case ParamTarget.DRUM_6_LEVEL: next.drumLevels = [...prev.drumLevels]; next.drumLevels[5] = ccVal; break
                    case ParamTarget.DRUM_7_LEVEL: next.drumLevels = [...prev.drumLevels]; next.drumLevels[6] = ccVal; break
                    case ParamTarget.DRUM_8_LEVEL: next.drumLevels = [...prev.drumLevels]; next.drumLevels[7] = ccVal; break
                    case ParamTarget.DRUM_1_PAN: next.drumPans = [...prev.drumPans]; next.drumPans[0] = ccVal; break
                    case ParamTarget.DRUM_2_PAN: next.drumPans = [...prev.drumPans]; next.drumPans[1] = ccVal; break
                    case ParamTarget.DRUM_3_PAN: next.drumPans = [...prev.drumPans]; next.drumPans[2] = ccVal; break
                    case ParamTarget.DRUM_4_PAN: next.drumPans = [...prev.drumPans]; next.drumPans[3] = ccVal; break
                    case ParamTarget.DRUM_5_PAN: next.drumPans = [...prev.drumPans]; next.drumPans[4] = ccVal; break
                    case ParamTarget.DRUM_6_PAN: next.drumPans = [...prev.drumPans]; next.drumPans[5] = ccVal; break
                    case ParamTarget.DRUM_7_PAN: next.drumPans = [...prev.drumPans]; next.drumPans[6] = ccVal; break
                    case ParamTarget.DRUM_8_PAN: next.drumPans = [...prev.drumPans]; next.drumPans[7] = ccVal; break
                    case ParamTarget.DRUM_MASTER_LEVEL: next.drumMaster = ccVal; break
                    case ParamTarget.SYNTH_LEVEL: next.synthLevel = ccVal; break
                    case ParamTarget.SYNTH_PAN: next.synthPan = ccVal; break
                    case ParamTarget.SYNTH_MASTER_LEVEL: next.synthMaster = ccVal; break
                    case ParamTarget.MASTER_OUTPUT: next.masterOut = ccVal; break
                    default: return prev  // No change for synth params here
                  }
                  return next
                })

                // Update synth params for synth-specific params
                setSynthParams(prev => {
                  const next = { ...prev }
                  switch (target) {
                    case ParamTarget.SYNTH_OSC1_WAVE: next.osc1Wave = ccToWave(ccVal); break
                    case ParamTarget.SYNTH_OSC2_WAVE: next.osc2Wave = ccToWave(ccVal); break
                    case ParamTarget.SYNTH_OSC1_LEVEL: next.osc1Level = ccToNorm(ccVal); break
                    case ParamTarget.SYNTH_OSC2_LEVEL: next.osc2Level = ccToNorm(ccVal); break
                    case ParamTarget.SYNTH_OSC2_DETUNE: next.osc2Detune = ccToSemi(ccVal); break
                    case ParamTarget.SYNTH_FILTER_CUTOFF: next.filterCutoff = ccToFreq(ccVal); break
                    case ParamTarget.SYNTH_FILTER_RES: next.filterRes = ccToNorm(ccVal); break
                    case ParamTarget.SYNTH_FILTER_ENV_AMT: next.filterEnvAmt = ccToNorm(ccVal); break
                    case ParamTarget.SYNTH_AMP_ATTACK: next.ampAttack = ccToTime(ccVal); break
                    case ParamTarget.SYNTH_AMP_DECAY: next.ampDecay = ccToTime(ccVal); break
                    case ParamTarget.SYNTH_AMP_SUSTAIN: next.ampSustain = ccToNorm(ccVal); break
                    case ParamTarget.SYNTH_AMP_RELEASE: next.ampRelease = ccToTime(ccVal); break
                    case ParamTarget.SYNTH_FILT_ATTACK: next.filtAttack = ccToTime(ccVal); break
                    case ParamTarget.SYNTH_FILT_DECAY: next.filtDecay = ccToTime(ccVal); break
                    case ParamTarget.SYNTH_FILT_SUSTAIN: next.filtSustain = ccToNorm(ccVal); break
                    case ParamTarget.SYNTH_FILT_RELEASE: next.filtRelease = ccToTime(ccVal); break
                    case ParamTarget.SYNTH_VEL_TO_AMP: next.velToAmp = ccToNorm(ccVal); break
                    case ParamTarget.SYNTH_VEL_TO_FILTER: next.velToFilter = ccToNorm(ccVal); break
                    default: return prev  // No change for mixer params here
                  }
                  return next
                })
              }
            }
          }
        }
        break
      case MSG_TRANSPORT:
        setTransport({
          playing: msg.playing,
          recording: msg.recording,
          bpm: msg.bpm,
        })
        break
      case MSG_VOICES:
        setVoiceState({ synth: msg.synth, drums: msg.drums })
        break
      case MSG_SYNTH_STATE:
        setSynthParams(msg.params)
        setPresetIndex(msg.presetIndex)
        break
      case MSG_CC_BANK:
        setCurrentBank(msg.bank as Bank)
        break
      case MSG_FADER_STATE:
        setFaderStates(msg.states)
        break
      case MSG_MIXER_STATE:
        setMixerState({
          drumLevels: msg.drumLevels,
          drumPans: msg.drumPans,
          drumMaster: msg.drumMaster,
          synthLevel: msg.synthLevel,
          synthPan: msg.synthPan,
          synthMaster: msg.synthMaster,
          masterOut: msg.masterOut,
        })
        break
      case MSG_RESOURCES:
        setResources({
          memoryUsed: msg.memoryUsed,
          memoryTotal: msg.memoryTotal,
          cpuLoad: msg.cpuLoad,
        })
        break
      case MSG_TRACK_STATE:
        // Update synth track states from firmware
        setSynthTrackStates(msg.tracks.map(t => ({
          status: t.status,
          frozenSlot: t.frozenSlot,
        })))
        break
      case MSG_PATTERN_DUMP:
        // Update pattern data for a track
        setPatternData(prev => {
          const next = [...prev]
          const trackId = msg.trackId

          if (msg.offset === 0) {
            // First chunk - replace events
            next[trackId] = [...msg.events]
          } else {
            // Subsequent chunk - append events
            next[trackId] = [...prev[trackId], ...msg.events]
          }
          return next
        })
        // Clear pending events for this track (confirmed data arrived)
        if (msg.offset === 0) {
          setPendingEvents(prev => {
            const next = [...prev]
            next[msg.trackId] = []
            return next
          })
        }
        break
      case MSG_PATTERN_CLEAR:
        // Clear a track's pattern data
        setPatternData(prev => {
          const next = [...prev]
          next[msg.trackId] = []
          return next
        })
        break
      default:
        addLog('>', `${getMessageTypeName((msg as ParsedMessage).type)}: ${JSON.stringify(msg)}`)
    }
  }, [addLog])

  const handleChecksumError = useCallback(() => {
    setChecksumErrors((prev) => prev + 1)
  }, [])

  // Transport command helpers
  const sendCommand = useCallback(async (type: number, payload?: Uint8Array) => {
    if (serialRef.current) {
      const msg = buildMessage(type, payload)
      await serialRef.current.send(msg)
    }
  }, [])

  const handlePlay = useCallback(() => {
    sendCommand(CMD_PLAY)
  }, [sendCommand])

  const handleStop = useCallback(() => {
    sendCommand(CMD_STOP)
  }, [sendCommand])

  const handleRecord = useCallback(() => {
    sendCommand(CMD_RECORD)
  }, [sendCommand])

  const handleTempoChange = useCallback((bpm: number) => {
    const payload = new Uint8Array(2)
    payload[0] = bpm & 0xff
    payload[1] = (bpm >> 8) & 0xff
    sendCommand(CMD_TEMPO, payload)
  }, [sendCommand])

  const handleSynthParamChange = useCallback((paramId: SynthParamId, value: number) => {
    if (serialRef.current) {
      const msg = buildSynthParamCommand(paramId, value)
      serialRef.current.send(msg)
    }
  }, [])

  const handleLoadFactoryPreset = useCallback((index: number) => {
    if (serialRef.current) {
      const msg = buildLoadPresetCommand(index)
      serialRef.current.send(msg)
    }
  }, [])

  const handleBankChange = useCallback((bank: Bank) => {
    if (serialRef.current) {
      const msg = buildSetBankCommand(bank)
      serialRef.current.send(msg)
    }
  }, [])

  const handleFreezeTrack = useCallback((trackId: number) => {
    if (serialRef.current) {
      const msg = buildFreezeTrackCommand(trackId)
      serialRef.current.send(msg)
    }
  }, [])

  const handleUnfreezeTrack = useCallback((trackId: number) => {
    if (serialRef.current) {
      const msg = buildUnfreezeTrackCommand(trackId)
      serialRef.current.send(msg)
    }
  }, [])

  const handleLoadUserPreset = useCallback((params: SynthParams) => {
    // For user presets, send each parameter individually
    // This is simpler than adding a new protocol message for full preset upload
    if (serialRef.current) {
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

      // Send each param - they'll be processed quickly on the Daisy side
      for (const [paramId, value] of paramMap) {
        const msg = buildSynthParamCommand(paramId, value)
        serialRef.current.send(msg)
      }
    }
  }, [])

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

    // Create parser for this connection
    const parser = new ProtocolParser(handleMessage, handleChecksumError)
    parserRef.current = parser

    const serial = new WebSerialPort({
      onData: (data) => {
        // Feed data to protocol parser
        parser.feed(data)
        // Also log raw hex for debugging (only when raw log is visible)
        if (showRawLog) {
          addLog('>', bytesToHex(data))
        }
      },
      onConnect: () => {
        setConnected(true)
        setTickCounter(0)
        setChecksumErrors(0)
        setMidiMessages([])
        setTransport({ playing: false, recording: false, bpm: 120 })
        setVoiceState({ synth: 0, drums: 0 })
        setSynthParams(getDefaultSynthParams())
        setPresetIndex(0)
        setCurrentBank(Bank.SYNTH)
        setFaderStates(Array(9).fill({ pickedUp: true, needsPickup: false }))
        setMixerState(getDefaultMixerState())
        addLog('<', '-- Connected --')
        // Request initial state from Daisy
        setTimeout(() => {
          serial.send(buildMessage(CMD_REQ_STATE))
          serial.send(buildRequestSynthCommand())
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
          transport={transport}
          position={position}
          onPlay={handlePlay}
          onStop={handleStop}
          onRecord={handleRecord}
          onTempoChange={handleTempoChange}
          connected={connected}
        />
      </div>

      {/* Tab Bar */}
      <div className="px-4 pt-3">
        <TabBar activeTab={activeTab} onTabChange={setActiveTab} />
      </div>

      {/* Main Content */}
      <main className="flex-1 p-4 space-y-4 overflow-y-auto">
        {/* Arrange Tab */}
        {activeTab === 'arrange' && (
          <ArrangeView
            memoryUsed={resources.memoryUsed}
            memoryTotal={resources.memoryTotal}
            cpuLoad={resources.cpuLoad}
            synthTracks={synthTrackStates.map((state, i) => ({
              id: i + 8,  // Track IDs 8-11 for synth
              status: state.status,
              frozenSlot: state.frozenSlot,
              events: patternData[i + 8] || [],
              pendingEvents: pendingEvents[i + 8] || [],
            }))}
            drumEvents={patternData.slice(0, 8)}
            pendingDrumEvents={pendingEvents.slice(0, 8)}
            playheadTick={tickCounter}
            playing={transport.playing}
            connected={connected}
            onFreezeTrack={handleFreezeTrack}
            onUnfreezeTrack={handleUnfreezeTrack}
          />
        )}

        {/* Synth Tab */}
        {activeTab === 'synth' && (
          <>
            <PresetManager
              currentPresetIndex={presetIndex}
              currentParams={synthParams}
              onLoadFactoryPreset={handleLoadFactoryPreset}
              onLoadUserPreset={handleLoadUserPreset}
              connected={connected}
            />
            <SynthPanel
              params={synthParams}
              onParamChange={handleSynthParamChange}
              connected={connected}
            />
          </>
        )}

        {/* Mix Tab */}
        {activeTab === 'mix' && (
          <CCControlPanel
            currentBank={currentBank}
            faderStates={faderStates}
            mixerState={mixerState}
            synthParams={synthParams}
            onBankChange={handleBankChange}
            connected={connected}
          />
        )}

        {/* Debug Tab */}
        {activeTab === 'debug' && (
          <>
            <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
              <MidiMonitor
                messages={midiMessages}
                onClear={() => setMidiMessages([])}
              />
              <EngineState
                tickCounter={tickCounter}
                checksumErrors={checksumErrors}
                transport={transport}
                position={position}
                synthVoices={voiceState.synth}
              />
            </div>

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
          </>
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
