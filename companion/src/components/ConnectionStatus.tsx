interface ConnectionStatusProps {
  connected: boolean
}

export default function ConnectionStatus({ connected }: ConnectionStatusProps) {
  return (
    <div className="flex items-center gap-2">
      <div
        className={`w-3 h-3 rounded-full ${
          connected ? 'bg-groove-green animate-pulse' : 'bg-groove-muted'
        }`}
      />
      <span className={connected ? 'text-groove-green' : 'text-groove-muted'}>
        {connected ? 'Connected to Daisy Pod' : 'Disconnected'}
      </span>
    </div>
  )
}
