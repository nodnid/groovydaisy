import { useState, useEffect, useCallback } from 'react'
import { SynthParams, FACTORY_PRESETS } from '../core/protocol'

const LOCAL_STORAGE_KEY = 'groovydaisy-user-presets'

interface UserPreset {
  name: string
  params: SynthParams
}

interface PresetManagerProps {
  currentPresetIndex: number
  currentParams: SynthParams
  onLoadFactoryPreset: (index: number) => void
  onLoadUserPreset: (params: SynthParams) => void
  connected: boolean
}

function loadUserPresets(): UserPreset[] {
  try {
    const stored = localStorage.getItem(LOCAL_STORAGE_KEY)
    if (stored) {
      return JSON.parse(stored)
    }
  } catch (e) {
    console.error('Failed to load user presets:', e)
  }
  return []
}

function saveUserPresets(presets: UserPreset[]) {
  try {
    localStorage.setItem(LOCAL_STORAGE_KEY, JSON.stringify(presets))
  } catch (e) {
    console.error('Failed to save user presets:', e)
  }
}

export default function PresetManager({
  currentPresetIndex,
  currentParams,
  onLoadFactoryPreset,
  onLoadUserPreset,
  connected,
}: PresetManagerProps) {
  const [userPresets, setUserPresets] = useState<UserPreset[]>([])
  const [showSaveDialog, setShowSaveDialog] = useState(false)
  const [newPresetName, setNewPresetName] = useState('')
  const [selectedUserPreset, setSelectedUserPreset] = useState<number>(-1)

  // Load user presets on mount
  useEffect(() => {
    setUserPresets(loadUserPresets())
  }, [])

  const handleSavePreset = useCallback(() => {
    if (!newPresetName.trim()) return

    const newPreset: UserPreset = {
      name: newPresetName.trim(),
      params: { ...currentParams },
    }

    const updated = [...userPresets, newPreset]
    setUserPresets(updated)
    saveUserPresets(updated)
    setShowSaveDialog(false)
    setNewPresetName('')
  }, [newPresetName, currentParams, userPresets])

  const handleDeletePreset = useCallback((index: number) => {
    const updated = userPresets.filter((_, i) => i !== index)
    setUserPresets(updated)
    saveUserPresets(updated)
    setSelectedUserPreset(-1)
  }, [userPresets])

  const handleLoadUserPreset = useCallback((index: number) => {
    if (index >= 0 && index < userPresets.length) {
      setSelectedUserPreset(index)
      onLoadUserPreset(userPresets[index].params)
    }
  }, [userPresets, onLoadUserPreset])

  return (
    <div className="bg-groove-panel border border-groove-border rounded-lg">
      <div className="px-4 py-3 border-b border-groove-border flex items-center justify-between">
        <h2 className="font-semibold text-groove-text">Presets</h2>
        <button
          onClick={() => setShowSaveDialog(true)}
          disabled={!connected}
          className="px-3 py-1 text-sm bg-groove-accent hover:bg-blue-500 text-white rounded
                     disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
        >
          Save Current
        </button>
      </div>

      <div className="p-4 space-y-4">
        {/* Factory Presets */}
        <div>
          <h3 className="text-xs font-semibold text-groove-muted mb-2">Factory Presets</h3>
          <div className="flex flex-wrap gap-2">
            {FACTORY_PRESETS.map((name, i) => (
              <button
                key={i}
                onClick={() => onLoadFactoryPreset(i)}
                disabled={!connected}
                className={`px-3 py-1.5 text-sm rounded transition-colors
                           ${currentPresetIndex === i && selectedUserPreset === -1
                             ? 'bg-groove-accent text-white'
                             : 'bg-groove-bg text-groove-muted hover:bg-groove-border hover:text-groove-text'
                           }
                           disabled:opacity-50 disabled:cursor-not-allowed`}
              >
                {name}
              </button>
            ))}
          </div>
        </div>

        {/* User Presets */}
        {userPresets.length > 0 && (
          <div>
            <h3 className="text-xs font-semibold text-groove-muted mb-2">User Presets</h3>
            <div className="flex flex-wrap gap-2">
              {userPresets.map((preset, i) => (
                <div key={i} className="group relative">
                  <button
                    onClick={() => handleLoadUserPreset(i)}
                    disabled={!connected}
                    className={`px-3 py-1.5 text-sm rounded transition-colors
                               ${selectedUserPreset === i
                                 ? 'bg-groove-green text-white'
                                 : 'bg-groove-bg text-groove-muted hover:bg-groove-border hover:text-groove-text'
                               }
                               disabled:opacity-50 disabled:cursor-not-allowed`}
                  >
                    {preset.name}
                  </button>
                  <button
                    onClick={() => handleDeletePreset(i)}
                    className="absolute -top-1 -right-1 w-4 h-4 bg-groove-red text-white rounded-full
                               text-xs opacity-0 group-hover:opacity-100 transition-opacity
                               flex items-center justify-center hover:bg-red-600"
                    title="Delete preset"
                  >
                    ×
                  </button>
                </div>
              ))}
            </div>
          </div>
        )}

        {/* Save Dialog */}
        {showSaveDialog && (
          <div className="fixed inset-0 bg-black/50 flex items-center justify-center z-50">
            <div className="bg-groove-panel border border-groove-border rounded-lg p-4 w-80">
              <h3 className="font-semibold text-groove-text mb-4">Save Preset</h3>
              <input
                type="text"
                value={newPresetName}
                onChange={(e) => setNewPresetName(e.target.value)}
                onKeyDown={(e) => {
                  if (e.key === 'Enter') handleSavePreset()
                  if (e.key === 'Escape') setShowSaveDialog(false)
                }}
                placeholder="Preset name..."
                autoFocus
                className="w-full px-3 py-2 bg-groove-bg border border-groove-border rounded
                           text-groove-text placeholder-groove-muted focus:border-groove-accent
                           focus:outline-none"
              />
              <div className="flex gap-2 mt-4">
                <button
                  onClick={() => setShowSaveDialog(false)}
                  className="flex-1 px-3 py-2 bg-groove-bg border border-groove-border rounded
                             text-groove-muted hover:text-groove-text hover:border-groove-muted
                             transition-colors"
                >
                  Cancel
                </button>
                <button
                  onClick={handleSavePreset}
                  disabled={!newPresetName.trim()}
                  className="flex-1 px-3 py-2 bg-groove-accent text-white rounded
                             hover:bg-blue-500 disabled:opacity-50 disabled:cursor-not-allowed
                             transition-colors"
                >
                  Save
                </button>
              </div>
            </div>
          </div>
        )}
      </div>
    </div>
  )
}
