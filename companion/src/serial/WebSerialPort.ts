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

  constructor(callbacks: SerialCallbacks) {
    this.callbacks = callbacks

    // Listen for device disconnect
    navigator.serial?.addEventListener('disconnect', () => {
      // When any serial device disconnects, check if it was ours
      if (this.port) {
        this.handleDisconnect()
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
      // Request port from user (we already checked isSupported so serial exists)
      this.port = await navigator.serial!.requestPort({
        filters: [
          // Daisy USB CDC - STM32 VID
          { usbVendorId: 0x0483 }
        ]
      })

      // Open with standard baud (doesn't matter for USB CDC, but required)
      await this.port.open({ baudRate: 115200 })

      // Get writer
      if (this.port.writable) {
        this.writer = this.port.writable.getWriter()
      }

      // Start reading
      this.startReading()

      this.callbacks.onConnect()
      return true
    } catch (error) {
      if ((error as Error).name !== 'NotFoundError') {
        // NotFoundError just means user cancelled - not a real error
        this.callbacks.onError(error as Error)
      }
      return false
    }
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
  }

  async disconnect(): Promise<void> {
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
