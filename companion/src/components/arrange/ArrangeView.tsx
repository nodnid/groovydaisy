import { useEffect, useRef, useState } from 'react'
import { PPQN } from '../../core/constants'
import {
  interpolateTick,
  type DeviceState,
} from '../../core/state'
import TrackLane from './TrackLane'
import TrackEditor from './TrackEditor'
import type { TrackEditArgs } from '../../core/protocol'
import CaptureStrip from './CaptureStrip'
import GroovePanel from './GroovePanel'
import ScenePanel from './ScenePanel'

interface ArrangeViewProps {
  device: DeviceState
  padBars: number
  keyBars: number
  guitarBars: number
  onPadBars: (bars: number) => void
  onKeyBars: (bars: number) => void
  onGuitarBars: (bars: number) => void
  onCapture: (source: number, bars: number) => void
  onUndo: () => void
  onMute: (slot: number, mute: boolean) => void
  onLevel: (slot: number, level: number) => void
  onDelete: (slot: number, gen: number) => void
  onEdit: (slot: number, gen: number, args: TrackEditArgs) => void
  onGroove: (param: number, value: number) => void
  onScene: (op: number, idx: number) => void
  connected: boolean
}

/**
 * The primary surface: DAW-style lanes of captured loops. Lane width is
 * proportional to loop length, so polymeter is visible at a glance; each
 * lane's playhead loops at its own rate against the shared clock.
 */
export default function ArrangeView({
  device,
  padBars,
  keyBars,
  guitarBars,
  onPadBars,
  onKeyBars,
  onGuitarBars,
  onCapture,
  onUndo,
  onMute,
  onLevel,
  onDelete,
  onEdit,
  onGroove,
  onScene,
  connected,
}: ArrangeViewProps) {
  const [nowTick, setNowTick] = useState(0)
  const [editingSlot, setEditingSlot] = useState<number | null>(null)
  const rafRef = useRef<number>(0)

  // One animation loop drives every lane's playhead
  useEffect(() => {
    if (!device.transport.playing) {
      setNowTick(device.sync.tick)
      return
    }
    const step = () => {
      setNowTick(
        interpolateTick(device.sync, device.transport, performance.now(), PPQN)
      )
      rafRef.current = requestAnimationFrame(step)
    }
    rafRef.current = requestAnimationFrame(step)
    return () => cancelAnimationFrame(rafRef.current)
  }, [device.transport, device.sync])

  const ordered = Object.values(device.tracks).sort(
    (a, b) => a.createdSeq - b.createdSeq
  )

  return (
    <div className="space-y-3">
      <CaptureStrip
        activity={device.srcActivity}
        captureFlash={device.captureFlash}
        pool={device.pool}
        tempoLocked={device.transport.tempoLocked}
        padBars={padBars}
        keyBars={keyBars}
        guitarBars={guitarBars}
        onPadBars={onPadBars}
        onKeyBars={onKeyBars}
        onGuitarBars={onGuitarBars}
        onCapture={onCapture}
        onUndo={onUndo}
        playing={device.transport.playing}
        hasTracks={ordered.length > 0}
        connected={connected}
      />

      <GroovePanel groove={device.groove} onGroove={onGroove} connected={connected} />

      <ScenePanel
        scenes={device.scenes}
        playing={device.transport.playing}
        onScene={onScene}
        connected={connected}
      />

      {ordered.length === 0 ? (
        <div className="bg-groove-panel border border-groove-border rounded-lg p-8 text-center">
          <p className="text-groove-muted">
            No loops yet — the box is listening while the clock runs.
          </p>
          <p className="text-groove-muted text-sm mt-1">
            Play something, watch the history ring fill, then hit Grab (or
            the KeyLab <span className="font-semibold">Live</span> button).
          </p>
        </div>
      ) : (
        <div className="space-y-2">
          {ordered.map((t) => (
            <TrackLane
              key={`${t.slot}-${t.gen}`}
              track={t}
              data={device.trackData[t.slot]}
              peaks={device.audioPeaks[t.slot]}
              strip={device.strips[t.slot]}
              nowTick={nowTick}
              playing={device.transport.playing}
              onMute={onMute}
              onLevel={onLevel}
              onDelete={onDelete}
              onEdit={onEdit}
              onOpenEditor={setEditingSlot}
              connected={connected}
            />
          ))}
        </div>
      )}

      {editingSlot !== null && device.tracks[editingSlot] && (
        <TrackEditor
          track={device.tracks[editingSlot]}
          data={device.trackData[editingSlot]}
          onEdit={onEdit}
          onClose={() => setEditingSlot(null)}
          connected={connected}
        />
      )}
    </div>
  )
}
