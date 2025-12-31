import { useMemo } from 'react'

export interface CCFaderProps {
  cc: number
  name: string
  value: number  // 0-127
  pickedUp: boolean
  needsPickup: boolean
  formatValue?: (value: number) => string
  disabled?: boolean
}

export default function CCFader({
  cc,
  name,
  value,
  pickedUp,
  needsPickup,
  formatValue,
  disabled = false,
}: CCFaderProps) {
  // Calculate display percentage (0-100)
  const percentage = (value / 127) * 100

  // Format the value for display
  const displayValue = useMemo(() => {
    if (formatValue) {
      return formatValue(value)
    }
    return value.toString()
  }, [value, formatValue])

  // Determine border color based on pickup state
  const borderColor = useMemo(() => {
    if (disabled) return 'border-groove-border'
    if (pickedUp) return 'border-groove-green'
    if (needsPickup) return 'border-groove-yellow'
    return 'border-groove-border'
  }, [disabled, pickedUp, needsPickup])

  return (
    <div className={`flex flex-col items-center gap-1 p-2 rounded border ${borderColor} bg-groove-panel`}>
      {/* CC number badge */}
      <div className="text-[10px] text-groove-muted font-mono">CC{cc}</div>

      {/* Fader track */}
      <div className="relative w-6 h-24 bg-groove-bg rounded">
        {/* Fill */}
        <div
          className="absolute bottom-0 left-0 right-0 bg-groove-accent rounded-b transition-all"
          style={{ height: `${percentage}%` }}
        />

        {/* Ghost marker (target value when not picked up) */}
        {needsPickup && !pickedUp && (
          <div
            className="absolute left-0 right-0 h-0.5 bg-groove-yellow opacity-50"
            style={{ bottom: `${percentage}%` }}
          />
        )}

        {/* Fader cap */}
        <div
          className="absolute left-0 right-0 h-2 bg-white rounded shadow transition-all"
          style={{ bottom: `calc(${percentage}% - 4px)` }}
        />
      </div>

      {/* Parameter name */}
      <div className="text-xs text-groove-text text-center font-medium truncate w-full" title={name}>
        {name}
      </div>

      {/* Value display */}
      <div className="text-xs text-groove-muted font-mono">
        {displayValue}
      </div>

      {/* Pickup indicator */}
      {needsPickup && !pickedUp && (
        <div className="text-[10px] text-groove-yellow">
          pickup
        </div>
      )}
    </div>
  )
}
