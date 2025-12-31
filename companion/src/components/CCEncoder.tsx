import { useMemo } from 'react'

export interface CCEncoderProps {
  cc: number
  name: string
  value: number  // 0-127
  formatValue?: (value: number) => string
  disabled?: boolean
  active?: boolean  // Flash on activity
}

export default function CCEncoder({
  cc,
  name,
  value,
  formatValue,
  disabled = false,
  active = false,
}: CCEncoderProps) {
  // Calculate rotation angle (0-127 maps to -135 to +135 degrees)
  const rotation = ((value / 127) * 270) - 135

  // Format the value for display
  const displayValue = useMemo(() => {
    if (formatValue) {
      return formatValue(value)
    }
    return value.toString()
  }, [value, formatValue])

  // SVG for the encoder knob
  const radius = 18
  const centerX = 24
  const centerY = 24

  // Calculate indicator position
  const indicatorAngle = (rotation - 90) * (Math.PI / 180)
  const indicatorX = centerX + Math.cos(indicatorAngle) * (radius - 4)
  const indicatorY = centerY + Math.sin(indicatorAngle) * (radius - 4)

  return (
    <div className={`flex flex-col items-center gap-1 p-2 rounded border ${disabled ? 'border-groove-border opacity-50' : 'border-groove-border'} bg-groove-panel`}>
      {/* CC number badge */}
      <div className="text-[10px] text-groove-muted font-mono">CC{cc}</div>

      {/* Encoder SVG */}
      <svg
        width="48"
        height="48"
        viewBox="0 0 48 48"
        className={`transition-all ${active ? 'filter brightness-125' : ''}`}
      >
        {/* Track arc (background) */}
        <circle
          cx={centerX}
          cy={centerY}
          r={radius}
          fill="none"
          stroke="#2A2D35"
          strokeWidth="4"
          strokeLinecap="round"
        />

        {/* Value arc */}
        <circle
          cx={centerX}
          cy={centerY}
          r={radius}
          fill="none"
          stroke="#3B82F6"
          strokeWidth="4"
          strokeLinecap="round"
          strokeDasharray={`${(value / 127) * 113} 113`}
          strokeDashoffset="28.25"
          transform={`rotate(-135 ${centerX} ${centerY})`}
        />

        {/* Knob body */}
        <circle
          cx={centerX}
          cy={centerY}
          r={radius - 6}
          fill="#1F2125"
          stroke="#3A3D45"
          strokeWidth="1"
        />

        {/* Position indicator */}
        <circle
          cx={indicatorX}
          cy={indicatorY}
          r="2"
          fill="#ffffff"
        />
      </svg>

      {/* Parameter name */}
      <div className="text-xs text-groove-text text-center font-medium truncate w-full" title={name}>
        {name}
      </div>

      {/* Value display */}
      <div className="text-xs text-groove-muted font-mono">
        {displayValue}
      </div>
    </div>
  )
}
