import type { SceneState } from '../../core/state'
import { NUM_SCENES, SCENE_SAVE, SCENE_GO } from '../../core/protocol'

interface ScenePanelProps {
  scenes: SceneState
  playing: boolean
  onScene: (op: number, idx: number) => void
  connected: boolean
}

/**
 * Scenes (horizon #2): each button is a saved mute-state snapshot.
 * Click = switch on the next bar line (verse/chorus at a campfire);
 * shift-click = save the current mutes into that slot. An armed scene
 * pulses until the bar line takes it.
 */
export default function ScenePanel({ scenes, playing, onScene, connected }: ScenePanelProps) {
  return (
    <div className="bg-groove-panel border border-groove-border rounded-lg px-4 py-2 flex items-center gap-3">
      <span className="text-xs font-bold uppercase tracking-wide text-groove-muted">
        Scenes
      </span>
      <div className="flex gap-1.5">
        {Array.from({ length: NUM_SCENES }).map((_, i) => {
          const defined = (scenes.definedMask >> i) & 1
          const isActive = scenes.active === i
          const isArmed = scenes.armed === i
          return (
            <button
              key={i}
              disabled={!connected}
              onClick={(e) =>
                onScene(e.shiftKey ? SCENE_SAVE : SCENE_GO, i)
              }
              title={
                defined
                  ? `Scene ${i + 1}: switch on the next bar line (shift-click to overwrite)`
                  : `Scene ${i + 1} is empty — shift-click to save the current mutes`
              }
              className={`w-8 h-8 rounded font-bold text-sm disabled:opacity-40 transition-colors ${
                isArmed
                  ? 'bg-groove-yellow text-black animate-pulse'
                  : isActive
                    ? 'bg-groove-accent text-white'
                    : defined
                      ? 'bg-groove-border text-groove-text hover:bg-groove-accent hover:text-white'
                      : 'bg-groove-bg border border-dashed border-groove-border text-groove-muted'
              }`}
            >
              {i + 1}
            </button>
          )
        })}
      </div>
      <span className="text-[11px] text-groove-muted ml-auto">
        click = go{playing ? ' (next bar)' : ''} · shift-click = save mutes
      </span>
    </div>
  )
}
