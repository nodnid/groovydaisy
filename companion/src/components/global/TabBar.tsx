export type TabId = 'arrange' | 'sound' | 'mix' | 'debug'

const TABS: Array<{ id: TabId; label: string }> = [
  { id: 'arrange', label: 'Arrange' },
  { id: 'sound', label: 'Sound' },
  { id: 'mix', label: 'Mix' },
  { id: 'debug', label: 'Debug' },
]

interface TabBarProps {
  active: TabId
  onChange: (tab: TabId) => void
}

export default function TabBar({ active, onChange }: TabBarProps) {
  return (
    <nav className="flex gap-1 px-4 pt-2 bg-groove-panel border-b border-groove-border">
      {TABS.map((t) => (
        <button
          key={t.id}
          onClick={() => onChange(t.id)}
          className={`px-4 py-2 rounded-t-lg text-sm font-semibold transition-colors ${
            active === t.id
              ? 'bg-groove-bg text-groove-text border border-b-0 border-groove-border'
              : 'text-groove-muted hover:text-groove-text'
          }`}
        >
          {t.label}
        </button>
      ))}
    </nav>
  )
}
