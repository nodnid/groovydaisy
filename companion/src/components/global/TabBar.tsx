/**
 * TabBar - Navigation between main views
 *
 * Four tabs: Arrange | Synth | Mix | Debug
 * Provides visual navigation and tab state
 */

export type TabId = 'arrange' | 'synth' | 'mix' | 'debug'

interface TabBarProps {
  activeTab: TabId
  onTabChange: (tab: TabId) => void
}

interface TabDef {
  id: TabId
  label: string
  icon: string
}

const TABS: TabDef[] = [
  { id: 'arrange', label: 'Arrange', icon: '🎹' },
  { id: 'synth', label: 'Synth', icon: '🎛️' },
  { id: 'mix', label: 'Mix', icon: '🎚️' },
  { id: 'debug', label: 'Debug', icon: '🔧' },
]

export default function TabBar({ activeTab, onTabChange }: TabBarProps) {
  return (
    <div className="flex gap-1 bg-groove-panel rounded-lg p-1 border border-groove-border">
      {TABS.map((tab) => (
        <button
          key={tab.id}
          onClick={() => onTabChange(tab.id)}
          className={`flex-1 px-4 py-2 rounded font-medium transition-colors flex items-center justify-center gap-2 ${
            activeTab === tab.id
              ? 'bg-groove-accent text-white'
              : 'text-groove-text-dim hover:bg-groove-border hover:text-groove-text'
          }`}
        >
          <span>{tab.icon}</span>
          <span className="hidden sm:inline">{tab.label}</span>
        </button>
      ))}
    </div>
  )
}
