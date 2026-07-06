// WebSerial API type definitions
interface SerialPort {
  readable: ReadableStream<Uint8Array> | null
  writable: WritableStream<Uint8Array> | null
  open(options: { baudRate: number }): Promise<void>
  close(): Promise<void>
}

interface SerialPortRequestOptions {
  filters?: Array<{ usbVendorId?: number; usbProductId?: number }>
}

interface Serial extends EventTarget {
  requestPort(options?: SerialPortRequestOptions): Promise<SerialPort>
  getPorts(): Promise<SerialPort[]>
}

declare global {
  interface Navigator {
    serial?: Serial
  }
}

export interface SerialCallbacks {
  onData: (data: Uint8Array) => void
  onConnect: () => void
  onDisconnect: () => void
  onError: (error: Error) => void
}

export class WebSerialPort {
  private port: SerialPort | null = null
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null
  private writer: WritableStreamDefaultWriter<Uint8Array> | null = null
  private callbacks: SerialCallbacks
  private reading = false
  private reconnectTimer: number | null = null
  private wantConnected = false // survives unplug: drives auto-reconnect

  constructor(callbacks: SerialCallbacks) {
    this.callbacks = callbacks

    // Listen for device disconnect
    navigator.serial?.addEventListener('disconnect', () => {
      // When any serial device disconnects, check if it was ours
      if (this.port) {
        this.handleDisconnect()
      }
    })

    // A granted device re-enumerating (flash cycle, cable wiggle) fires
    // 'connect' — grab it immediately instead of waiting for the poll
    navigator.serial?.addEventListener('connect', () => {
      if (this.wantConnected && !this.port) {
        void this.tryReconnect()
      }
    })
  }

  static isSupported(): boolean {
    return 'serial' in navigator
  }

  async connect(): Promise<boolean> {
    if (!WebSerialPort.isSupported()) {
      this.callbacks.onError(new Error('WebSerial not supported in this browser'))
      return false
    }

    try {
      // Reuse an already-granted port when there's exactly one (one-click
      // reconnect after the first session); otherwise show the picker
      const granted = (await navigator.serial!.getPorts()) ?? []
      if (granted.length === 1) {
        this.port = granted[0]
      } else {
        this.port = await navigator.serial!.requestPort({
          filters: [
            // Daisy USB CDC - STM32 VID
            { usbVendorId: 0x0483 }
          ]
        })
      }

      await this.openAndStart()
      this.wantConnected = true
      return true
    } catch (error) {
      this.port = null
      if ((error as Error).name !== 'NotFoundError') {
        // NotFoundError just means user cancelled - not a real error
        this.callbacks.onError(error as Error)
      }
      return false
    }
  }

  /** Open the selected port, wire the writer, start the read loop. */
  private async openAndStart(): Promise<void> {
    if (!this.port) throw new Error('no port selected')

    // Open with standard baud (doesn't matter for USB CDC, but required)
    await this.port.open({ baudRate: 115200 })

    if (this.port.writable) {
      this.writer = this.port.writable.getWriter()
    }

    this.startReading()
    this.callbacks.onConnect()
  }

  /**
   * Auto-reconnect (the reconnectTimer finally lives — ROADMAP Phase 6):
   * after an unexpected disconnect, poll the granted-ports list until the
   * Daisy re-enumerates, reopen, and let onConnect re-hydrate via
   * CMD_REQ_STATE. No user gesture needed — the grant persists.
   */
  private async tryReconnect(): Promise<void> {
    if (!this.wantConnected || this.port) return
    try {
      const granted = (await navigator.serial!.getPorts()) ?? []
      if (granted.length > 0) {
        this.port = granted[0]
        await this.openAndStart()
        return // reconnected — no further polling
      }
    } catch {
      this.port = null
      this.writer = null
    }
    this.reconnectTimer = window.setTimeout(() => {
      this.reconnectTimer = null
      void this.tryReconnect()
    }, 1000)
  }

  private async startReading(): Promise<void> {
    if (!this.port?.readable) return

    this.reading = true
    const reader = this.port.readable.getReader()
    this.reader = reader

    try {
      while (this.reading) {
        const { value, done } = await reader.read()
        if (done) break
        if (value) {
          this.callbacks.onData(value)
        }
      }
    } catch (error) {
      if (this.reading) {
        // Only report errors if we didn't intentionally stop
        this.callbacks.onError(error as Error)
      }
    } finally {
      reader.releaseLock()
      this.reader = null
    }
  }

  async send(data: Uint8Array): Promise<void> {
    if (!this.writer) {
      throw new Error('Not connected')
    }
    await this.writer.write(data)
  }

  async sendString(text: string): Promise<void> {
    const encoder = new TextEncoder()
    await this.send(encoder.encode(text))
  }

  private handleDisconnect(): void {
    this.reading = false
    this.reader = null
    this.writer = null
    this.port = null
    this.callbacks.onDisconnect()
    if (this.wantConnected) {
      void this.tryReconnect() // unexpected: chase the device
    }
  }

  async disconnect(): Promise<void> {
    this.wantConnected = false // user asked: stay down
    this.reading = false

    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer)
      this.reconnectTimer = null
    }

    try {
      this.reader?.releaseLock()
      this.writer?.releaseLock()
      await this.port?.close()
    } catch {
      // Ignore errors during disconnect
    }

    this.reader = null
    this.writer = null
    this.port = null
    this.callbacks.onDisconnect()
  }

  isConnected(): boolean {
    return this.port !== null && this.reading
  }
}

// Helper to format bytes as hex string
export function bytesToHex(bytes: Uint8Array): string {
  return Array.from(bytes)
    .map((b) => b.toString(16).padStart(2, '0').toUpperCase())
    .join(' ')
}
