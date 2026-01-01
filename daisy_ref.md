# CLAUDE.md - Daisy Audio Development Reference

This file provides guidance to Claude Code when working with Daisy embedded audio projects.

---

## Project Overview

DaisyExamples is a collection of example projects for the Daisy embedded audio platform by Electro-Smith. It targets the STM32H750 ARM Cortex-M7 microcontroller running at 480MHz with hardware floating point.

### Repository Structure
- **libDaisy/** - Hardware abstraction library (git submodule)
- **DaisySP/** - DSP library with audio processing modules (git submodule)
- **seed/, pod/, patch/, petal/, field/, versio/, legio/, patch_sm/** - Examples organized by hardware platform
- **utils/** - Project templates and helper scripts
- **ci/** - Build scripts for continuous integration

---

## Build Commands

```bash
# Build both libraries (required before building examples)
./ci/build_libs.sh

# Build a single example (from the example's directory)
cd seed/Blink && make

# Build all examples
./ci/build_examples.py

# Rebuild everything (libraries + all examples)
./rebuild_all.sh

# Flash to Daisy via USB (after entering bootloader mode)
make program-dfu

# Flash via JTAG/SWD (STLink)
make program

# Check code style (requires clang-format-10)
./ci/local_style_check.sh
```

### DFU Mode
Hold **BOOT**, press **RESET**, release both.

### Bootloader (for larger programs)
```makefile
APP_TYPE = BOOT_SRAM   # Up to 480KB, runs from SRAM (fast)
APP_TYPE = BOOT_QSPI   # Up to ~8MB, runs from QSPI flash (slower)
```

1. Flash bootloader first: `make program-boot` (in DFU mode)
2. Flash app: `make program-dfu`

**Alternative:** Put `.bin` file on SD card root, insert and reset - bootloader auto-flashes.

**Bootloader behavior:**
- 2.5 sec grace period on boot (LED pulses)
- Press BOOT to extend grace period
- SOS LED pattern = error (usually invalid binary)

### Creating New Projects
```bash
# Create from template (creates passthrough audio project)
./helper.py create <platform>/<ProjectName> --board <platform>

# Copy existing project
./helper.py copy <dest> --source <source>

# Update debug configuration files
./helper.py update <path/to/project>
```

Supported boards: seed, pod, patch, field, petal, versio, legio, patch_sm

### Example Makefile Structure
```makefile
TARGET = ProjectName
CPP_SOURCES = ProjectName.cpp
LIBDAISY_DIR = ../../libDaisy
DAISYSP_DIR = ../../DaisySP
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
```

---

## Quick Start Template (Pod)

```cpp
#include "daisy_pod.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

DaisyPod hw;

void AudioCallback(AudioHandle::InputBuffer in,
                   AudioHandle::OutputBuffer out,
                   size_t size) {
    hw.ProcessAllControls();
    
    for (size_t i = 0; i < size; i++) {
        out[0][i] = in[0][i];  // Left
        out[1][i] = in[1][i];  // Right
    }
}

int main(void) {
    hw.Init();
    hw.SetAudioBlockSize(48);
    hw.StartAdc();
    hw.StartAudio(AudioCallback);
    while(1) {}
}
```

---

## Hardware Platform Classes

Each platform provides a pre-configured hardware class:

| Class | Header | Description |
|-------|--------|-------------|
| `DaisySeed` | `daisy_seed.h` | Base SOM - requires `hw.Configure()` before `hw.Init()` |
| `DaisyPod` | `daisy_pod.h` | 2 knobs, 2 buttons, encoder, RGB LEDs |
| `DaisyPatch` | `daisy_patch.h` | Eurorack module - 4-channel audio, OLED display, CV I/O |
| `DaisyPetal` | `daisy_petal.h` | Guitar pedal - 6 knobs, footswitch, expression pedal |
| `DaisyField` | `daisy_field.h` | Keyboard controller - 8 knobs, 8 faders, 16 keys |
| `DaisyVersio` | `daisy_versio.h` | Eurorack module - 7 knobs, 2 buttons, stereo audio |
| `DaisyLegio` | `daisy_legio.h` | Eurorack module - 4 knobs, 2 switches |
| `DaisyPatchSM` | `daisy_patch_sm.h` | Submodule (uses `patch_sm` namespace, `IN_L/R`, `OUT_L/R` macros) |

### Common Initialization Pattern
```cpp
hw.Init();
hw.SetAudioBlockSize(4);  // Samples per callback (4-256)
hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
hw.StartAdc();            // Start ADC for knobs/CV (not needed for Seed)
hw.StartAudio(AudioCallback);
```

### Audio Callback Types

**Non-interleaved** (preferred) - see Quick Start Template above.

**Interleaved**:
```cpp
void AudioCallback(AudioHandle::InterleavingInputBuffer in,
                   AudioHandle::InterleavingOutputBuffer out, size_t size)
{
    for (size_t i = 0; i < size; i += 2)
    {
        out[i] = in[i];      // Left
        out[i+1] = in[i+1];  // Right
    }
}
```

---

## Daisy Pod Hardware Reference

### Physical Controls

| Control | Access | Type |
|---------|--------|------|
| Knob 1 | `hw.knob1.Value()` | 0.0-1.0 float |
| Knob 2 | `hw.knob2.Value()` | 0.0-1.0 float |
| Button 1 | `hw.button1.Pressed()` / `.RisingEdge()` / `.FallingEdge()` | bool |
| Button 2 | `hw.button2.Pressed()` / `.RisingEdge()` / `.FallingEdge()` | bool |
| Encoder rotation | `hw.encoder.Increment()` | -1, 0, or +1 |
| Encoder click | `hw.encoder.RisingEdge()` | bool |
| LED 1 | `hw.led1.Set(r, g, b)` then `hw.UpdateLeds()` | 0.0-1.0 per channel |
| LED 2 | `hw.led2.Set(r, g, b)` then `hw.UpdateLeds()` | 0.0-1.0 per channel |

**Important:** Call `hw.ProcessAllControls()` at start of audio callback to update control values.

### Pod Pinout (for expansion)

| Function | Pin | Function | Pin |
|----------|-----|----------|-----|
| LED_1_R | D20 | SPI1_CS | D7 |
| LED_1_G | D19 | SPI1_SCK | D8 |
| LED_1_B | D18 | SPI1_MISO | D9 |
| LED_2_R | D17 | SPI1_MOSI | D10 |
| LED_2_G | D24 | I2C (see below) | D11/D12 |
| LED_2_B | D23 | | |
| BUTTON_1 | D27 | ENC_CLICK | D13 |
| BUTTON_2 | D28 | USB D+ | D30 |
| KNOB_1 | A6/D21 | USB D- | D29 |
| KNOB_2 | A0/D15 | Extra ADC | A7/D22, A1/D16 |
| ENC_A | D26 | UART_RX (MIDI) | D14 |
| ENC_B | D25 | | |

### MIDI
```cpp
hw.midi.StartReceive();

// In main loop or callback:
hw.midi.Listen();
while (hw.midi.HasEvents()) {
    MidiEvent e = hw.midi.PopEvent();
    if (e.type == NoteOn) {
        NoteOnEvent n = e.AsNoteOn();
        // n.note, n.velocity, n.channel
    } else if (e.type == ControlChange) {
        ControlChangeEvent cc = e.AsControlChange();
        // cc.control_number, cc.value
    }
}
```

---

## libDaisy Architecture

Hardware abstraction library providing access to Daisy hardware. All classes are in the `daisy` namespace.

### Source Organization (libDaisy/src/)
- **sys/** - System configuration (clocks, DMA, interrupts)
- **per/** - MCU peripherals: ADC, DAC, GPIO, I2C, SPI, UART, SAI (audio), QSPI, SDMMC, timers
- **dev/** - External device drivers: audio codecs (AK4556, PCM3060, WM8731), OLED displays (SSD130x), LED drivers, flash chips, sensors
- **hid/** - User interface: audio engine, MIDI, encoders, switches, LEDs, parameter mapping, USB
- **ui/** - Menu systems and event handling
- **util/** - Internal utilities

### GPIO
```cpp
GPIO pin;
pin.Init(D0, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
bool state = pin.Read();

pin.Init(D1, GPIO::Mode::OUTPUT);
pin.Write(true);
```

Modes: `INPUT`, `OUTPUT`, `OUTPUT_OD`, `ANALOG`
Pull: `NOPULL`, `PULLUP`, `PULLDOWN`

### Extra ADC Channels (beyond Pod's knobs)
```cpp
AdcChannelConfig cfg[2];
cfg[0].InitSingle(A7);  // Use Pin A7
cfg[1].InitSingle(A1);  // Use Pin A1
hw.seed.adc.Init(cfg, 2);
hw.seed.adc.Start();

float val = hw.seed.adc.GetFloat(0);  // 0.0-1.0
int raw = hw.seed.adc.Get(0);         // 0-65535
```

### SPI
```cpp
SpiHandle spi;
SpiHandle::Config cfg;
cfg.periph = SpiHandle::Config::Peripheral::SPI_1;
cfg.mode = SpiHandle::Config::Mode::MASTER;
cfg.direction = SpiHandle::Config::Direction::TWO_LINES;
cfg.nss = SpiHandle::Config::NSS::HARD_OUTPUT;  // or SOFT for manual CS
cfg.pin_config.sclk = seed::D8;
cfg.pin_config.miso = seed::D9;
cfg.pin_config.mosi = seed::D10;
cfg.pin_config.nss = seed::D7;
// Optional: cfg.baud_prescaler = SpiHandle::Config::BaudPrescaler::PS_8;
spi.Init(cfg);

uint8_t tx[4] = {0, 1, 2, 3};
uint8_t rx[4];
spi.BlockingTransmit(tx, 4);
spi.BlockingReceive(rx, 4);
spi.BlockingTransmitAndReceive(tx, rx, 4);

// DMA (non-blocking) - buffers must use DMA_BUFFER_MEM_SECTION
spi.DmaTransmit(tx_dma, 4, NULL, NULL, NULL);
```

### I2C
```cpp
I2CHandle i2c;
I2CHandle::Config cfg;
cfg.periph = I2CHandle::Config::Peripheral::I2C_1;
cfg.mode = I2CHandle::Config::Mode::I2C_MASTER;
cfg.speed = I2CHandle::Config::Speed::I2C_400KHZ;  // or I2C_100KHZ, I2C_1MHZ
cfg.pin_config.scl = seed::D11;
cfg.pin_config.sda = seed::D12;
i2c.Init(cfg);

// Basic transfer
i2c.TransmitBlocking(addr, data, len, timeout_ms);
i2c.ReceiveBlocking(addr, buffer, len, timeout_ms);

// Register read/write (common pattern)
i2c.WriteDataAtAddress(addr, reg, 1, data, len, timeout_ms);
i2c.ReadDataAtAddress(addr, reg, 1, buffer, len, timeout_ms);

// Note: I2C_4 does NOT support DMA
```

**Daisy Seed I2C Pins:**
| Peripheral | SDA | SCL |
|------------|-----|-----|
| I2C_1 | D12 | D11 |
| I2C_4 | D14 | D13 |

---

## DaisySP DSP Library

Portable DSP library in the `daisysp` namespace. Include via `#include "daisysp.h"`.

### DSP Module Pattern
```cpp
Oscillator osc;
osc.Init(samplerate);
osc.SetFreq(440.0f);
osc.SetAmp(0.5f);
osc.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
float sample = osc.Process();
```

---

### Oscillators

```cpp
Oscillator osc;
osc.Init(sample_rate);
osc.SetFreq(440.0f);
osc.SetAmp(0.5f);
osc.SetWaveform(Oscillator::WAVE_SIN);
// Waveforms: WAVE_SIN, WAVE_TRI, WAVE_SAW, WAVE_RAMP, WAVE_SQUARE,
//            WAVE_POLYBLEP_TRI, WAVE_POLYBLEP_SAW, WAVE_POLYBLEP_SQUARE
float sample = osc.Process();
```

| Class | Description | Key Methods |
|-------|-------------|-------------|
| `Oscillator` | Multi-waveform osc | `Init(sr)`, `SetFreq/Amp/Waveform(val)`, `PhaseAdd(phase)`, `Reset()`, `Process()` |
| `BlOsc` | Band-limited osc | `Init(sr)`, `SetFreq/Amp/Waveform/PwmDuty(val)`, `Reset()`, `Process()` |
| `VariableSawOscillator` | Variable saw | `Init(sr)`, `SetFreq/Pw/Waveshape(val)`, `Process()` |
| `VariableShapeOscillator` | Morphing osc | `Init(sr)`, `SetFreq/Pw/Waveshape/Sync/SyncFreq(val)`, `Process()` |
| `FormantOscillator` | Formant osc | `Init(sr)`, `SetFreq/Formant/Carrierform/Modform/PhaseShift/Pw(val)`, `Process()` |
| `GrainletOscillator` | Granular noise osc | `Init(sr)`, `SetFreq/Formant/Shape/Bleed(val)`, `Process()` |
| `HarmonicOscillator<N>` | Additive osc | `Init(sr)`, `SetFreq/Amp/FirstHarmIdx(val)`, `SetAmplitudes(amps)`, `Process()` |
| `VosimOscillator` | Voice sim | `Init(sr)`, `SetFreq/Form1Freq/Form2Freq/Shape(val)`, `Process()` |
| `ZOscillator` | Complex osc | `Init(sr)`, `SetFreq/Formant/Shape/Mode(val)`, `Process()` |
| `OscillatorBank` | Oscillator bank | `Init(sr)`, `SetAmplitudes/Frequencies/Gains(array)`, `Process()` |
| `Fm2` | 2-op FM | `Init(sr)`, `SetFrequency/Ratio/Index(val)`, `Reset()`, `Process()` |

---

### Noise

```cpp
WhiteNoise noise;
noise.Init();
noise.SetAmp(0.5f);
float sample = noise.Process();
```

| Class | Description | Key Methods |
|-------|-------------|-------------|
| `WhiteNoise` | White noise | `Init()`, `SetAmp(amp)`, `Process()` |
| `Dust` | Random impulses | `Init()`, `SetDensity(density)`, `Process()` |
| `ClockedNoise` | Pitched random | `Init(sr)`, `SetFreq(freq)`, `SetSync(sync)`, `Sync()`, `Process()` |
| `FractalNoise` | Fractal brownian motion | `Init(sr)`, `SetFreq/Color(val)`, `Process()` |
| `Particle` | Particle noise | `Init()`, `SetFreq/Resonance/Randomness(val)`, `SetSpread(spread)`, `SetSync(sync)`, `Process()` |

---

### Filters

```cpp
// State Variable Filter (most versatile)
Svf filt;
filt.Init(sample_rate);
filt.SetFreq(1000.0f);
filt.SetRes(0.5f);  // 0.0-1.0
filt.Process(input);
float lp = filt.Low();
float hp = filt.High();
float bp = filt.Band();
float notch = filt.Notch();

// Moog Ladder
MoogLadder moog;
moog.Init(sample_rate);
moog.SetFreq(2000.0f);
moog.SetRes(0.7f);
float out = moog.Process(input);

// Simple one-pole lowpass
Tone tone;
tone.Init(sample_rate);
tone.SetFreq(1000.0f);
float out = tone.Process(input);
```

| Class | Description | Key Methods |
|-------|-------------|-------------|
| `Svf` | State variable filter | `Init(sr)`, `SetFreq(freq)`, `SetRes(res)`, `SetDrive(drive)`, `Process(in)`, `Low/High/Band/Notch/Peak()` |
| `MoogLadder` | Moog ladder filter | `Init(sr)`, `SetFreq(freq)`, `SetRes(res)`, `Process(in)` |
| `OnePole` | Simple 6dB/oct filter | `Init()`, `SetFreq(freq)`, `Process(in)`, `ProcessHp(in)` |
| `Biquad` | Biquad filter | `Init(sr)`, `SetCutoff/Res(val)`, `Process(in)` |
| `Tone` | One-pole lowpass | `Init(sr)`, `SetFreq(freq)`, `Process(in)` |
| `ATone` | One-pole highpass | `Init(sr)`, `SetFreq(freq)`, `Process(in)` |
| `Mode` | Resonant bandpass | `Init(sr)`, `SetFreq(freq)`, `SetQ(q)`, `Clear()`, `Process(in)` |
| `Comb` | Comb filter | `Init(sr, buff, size)`, `SetFreq/Revtime(val)`, `Process(in)` |
| `Allpass` | Allpass filter | `Init(sr, buff, size)`, `SetFreq(freq)`, `Process(in)` |
| `NlFilt` | Non-linear filter | `Init()`, `SetCoefficients(a, b, d, C, L)`, `Process(in)` |
| `Fir` | FIR filter | `Init(coeffs, size, reverse)`, `Process(in)` |
| `Soap` | Second-order allpass | `Init(sr)`, `SetCenterFreq/FilterBandwidth(val)`, `Process(in)` |
| `DcBlock` | DC offset removal | `Init(sr)`, `Process(in)` |
| `LadderFilter` | Ladder filter variant | `Init(sr)`, `SetFreq(freq)`, `SetRes(res)`, `Process(in)` |

---

### Envelopes

```cpp
// ADSR
Adsr env;
env.Init(sample_rate);
env.SetTime(ADSR_SEG_ATTACK, 0.01f);
env.SetTime(ADSR_SEG_DECAY, 0.1f);
env.SetSustainLevel(0.7f);
env.SetTime(ADSR_SEG_RELEASE, 0.5f);
float amp = env.Process(gate);  // gate = true while held

// AD (trigger-based)
AdEnv env;
env.Init(sample_rate);
env.SetTime(ADENV_SEG_ATTACK, 0.01f);
env.SetTime(ADENV_SEG_DECAY, 0.5f);
env.SetCurve(-10.0f);  // negative = exponential
env.Trigger();
float val = env.Process();
```

| Class | Description | Key Methods |
|-------|-------------|-------------|
| `Adsr` | ADSR envelope | `Init(sr)`, `SetTime(seg, time)`, `SetAttackTime/DecayTime/ReleaseTime(time)`, `SetSustainLevel(sus)`, `Process(gate)`, `Retrigger(hard)`, `GetCurrentSegment()`, `IsRunning()` |
| `AdEnv` | AD envelope | `Init(sr)`, `SetTime(seg, time)`, `SetCurve(scalar)`, `SetMin/Max(value)`, `Trigger()`, `Process()`, `GetCurrentSegment()`, `IsRunning()` |
| `Line` | Linear interpolator | `Init(sr)`, `Start(start, end, time)`, `Process()`, `IsFinished()` |
| `Phasor` | Phase accumulator (0-1 ramp) | `Init(sr)`, `SetFreq(freq)`, `Process()` |

---

### Effects

```cpp
// Reverb (stereo)
ReverbSc verb;
verb.Init(sample_rate);
verb.SetFeedback(0.85f);
verb.SetLpFreq(10000.0f);
float outL, outR;
verb.Process(inL, inR, &outL, &outR);

// Delay
DelayLine<float, 48000> del;  // template param is max size
del.Init();
del.SetDelay(24000.0f);  // in samples
del.Write(input);
float delayed = del.Read();

// Chorus (stereo out)
Chorus chorus;
chorus.Init(sample_rate);
chorus.SetLfoFreq(0.5f);
chorus.SetLfoDepth(0.5f);
float outL, outR;
chorus.Process(input, &outL, &outR);

// Overdrive
Overdrive od;
od.Init();
od.SetDrive(0.7f);
float out = od.Process(input);

// Compressor
Compressor comp;
comp.Init(sample_rate);
comp.SetThreshold(-20.0f);
comp.SetRatio(4.0f);
comp.SetAttack(0.01f);
comp.SetRelease(0.1f);
float out = comp.Process(input);
```

| Class | Description | Key Methods |
|-------|-------------|-------------|
| `ReverbSc` | Stereo reverb | `Init(sr)`, `SetFeedback(fb)`, `SetLpFreq(freq)`, `Process(inL, inR, *outL, *outR)` |
| `DelayLine<T,size>` | Circular delay buffer | `Init()`, `SetDelay(samples)`, `Write(in)`, `Read()`, `ReadHermite(delay)`, `Allpass(in, coef)` |
| `Chorus` | Stereo chorus | `Init(sr)`, `SetLfoFreq/LfoDepth/Feedback/Delay(val)`, `SetPan(left, right)`, `Process(in)`, `GetLeft/Right()` |
| `Flanger` | Flanging effect | `Init(sr)`, `SetLfoFreq/LfoDepth/Feedback/Delay(val)`, `Process(in)` |
| `Phaser` | Phase shifting | `Init(sr)`, `SetLfoFreq/LfoDepth/Feedback/Freq(val)`, `SetPoles(poles)`, `Process(in)` |
| `Overdrive` | Soft-clip distortion | `Init()`, `SetDrive(drive)`, `Process(in)` |
| `Decimator` | Bitcrusher/downsampler | `Init()`, `SetDownsampleFactor(factor)`, `SetBitcrushFactor(factor)`, `SetBitDepth(depth)`, `SetSmoothCrushing(smooth)`, `Process(in)` |
| `Bitcrush` | Bit reduction | `Init(sr)`, `SetBitDepth/CrushRate(val)`, `Process(in)` |
| `Autowah` | Envelope follower wah | `Init(sr)`, `SetWah/DryWet/Level(val)`, `Process(in)` |
| `Tremolo` | Amplitude modulation | `Init(sr)`, `SetFreq/Depth/Waveform(val)`, `Process(in)` |
| `Wavefolder` | Wave folding | `Init()`, `SetGain(gain)`, `Process(in)` |
| `Fold` | Wave folder | `Init()`, `SetIncrement(inc)`, `Process(in)` |
| `SampleRateReducer` | Sample rate reduction | `Init()`, `SetFreq(freq)`, `Process(in)` |
| `PitchShifter` | Time-domain pitch shift | `Init(sr)`, `SetTransposition(semitones)`, `SetDelSize(size)`, `SetFun(fun)`, `Process(in)` |
| `Compressor` | Dynamic range compressor | `Init(sr)`, `SetRatio/Threshold/Attack/Release/Makeup(val)`, `Process(in)`, `GetGain()`, `AutoMakeup(in)` |
| `Limiter` | Peak limiter | `Init()`, `ProcessBlock(in, out, size, pre_gain)` |
| `Crossfade` | Equal-power crossfade | `Init(curve)`, `SetPos(pos)`, `SetCurve(curve)`, `Process(in1, in2)` |
| `Balance` | Level matching | `Init(sr)`, `SetCutoff(freq)`, `Process(sig, comp)` |

---

### Physical Modeling

```cpp
// Karplus-Strong string - REQUIRES EXTERNAL BUFFER
float DSY_SDRAM_BSS pluck_buf[256];
Pluck pluck;
pluck.Init(sample_rate, pluck_buf, 256, PLUCK_MODE_RECURSIVE);
pluck.SetFreq(440.0f);
pluck.SetAmp(0.5f);
pluck.SetDecay(0.95f);
pluck.Trig();
float out = pluck.Process();
```

| Class | Description | Key Methods |
|-------|-------------|-------------|
| `Pluck` | Simplified pluck | `Init(sr, buf, size, mode)`, `SetFreq/Amp/Decay(val)`, `Trig()`, `Process(trig)` |
| `KarplusString` | Plucked string | `Init(sr)`, `SetFreq/Brightness/Damping/Nonlinearity(val)`, `Reset()`, `Process(in)`, `Pluck(freq, amp)` |
| `PolyPluck` | Polyphonic pluck | `Init(sr)`, `SetBrightness/Decay/Damping(val)`, `Process(trig, note)` |
| `String` | String model | `Init(sr)`, `SetFreq(freq)`, `Process(in)` |
| `StringVoice` | String synthesis | `Init(sr)`, `SetFreq/Accent/Structure/Brightness/Damping(val)`, `Trig()`, `Process()` |
| `Resonator` | Modal resonator | `Init(sr)`, `SetFreq/Structure/Brightness/Damping/Position(val)`, `Process(in)` |
| `ModalVoice` | Modal synthesis | `Init(sr)`, `SetFreq/Accent/Structure/Brightness/Damping/Sustain(val)`, `SetAux(val)`, `Trig()`, `Process(sustain)` |
| `Drip` | Water drop sim | `Init(sr, dettack)`, `SetDamp/EmitFreq/Intensity/DecayTime/TubeRes1/TubeRes2(val)`, `Trig()`, `Process(trig)` |

---

### Drums

```cpp
AnalogBassDrum bd;
bd.Init(sample_rate);
bd.SetFreq(60.0f);
bd.SetDecay(0.5f);
bd.Trig();
float out = bd.Process();
```

| Class | Description | Key Methods |
|-------|-------------|-------------|
| `AnalogBassDrum` | 808-style bass drum | `Init(sr)`, `SetFreq/Tone/Decay/AttackFmAmount/SelfFmAmount(val)`, `SetAccent(val)`, `Trig()`, `Process(trigger)` |
| `AnalogSnareDrum` | 808-style snare | `Init(sr)`, `SetFreq/Tone/Decay/Snappy/Accent(val)`, `Trig()`, `Process(trigger)` |
| `SyntheticBassDrum` | Digital bass drum | `Init(sr)`, `SetFreq/Accent/FmEnvAmount/DecayTime(val)`, `SetDirtiness/FmEnvDecay/Tone(val)`, `Trig()`, `Process(trigger)` |
| `SyntheticSnareDrum` | Digital snare | `Init(sr)`, `SetFreq/Accent/DecayTime/Snappy/Tone/FmAmount(val)`, `Trig()`, `Process(trigger)` |
| `HiHat` | Hi-hat synthesis | `Init(sr)`, `SetFreq/Tone/Decay/Noisiness/Accent(val)`, `SetSustain(sustain)`, `Trig()`, `Process(trigger)` |

---

### Utilities

```cpp
float freq = mtof(60.0f);  // MIDI to freq: 60 -> 261.63 Hz

// Map 0-1 to range with curve
float freq = fmap(knob, 20.0f, 20000.0f, Mapping::LOG);
// Curves: LINEAR, LOG, EXP

float clamped = fclamp(val, 0.0f, 1.0f);

// One-pole smoothing (call every sample)
fonepole(smoothed, target, 0.001f);  // coeff = 1/(time * sr)

// Metronome
Metro metro;
metro.Init(2.0f, sample_rate);  // 2 Hz
if (metro.Process()) { /* tick */ }

// Portamento
Port port;
port.Init(sample_rate, 0.1f);  // 100ms glide
float smoothed = port.Process(target);
```

| Class/Function | Description | Key Methods/Signature |
|----------------|-------------|----------------------|
| `mtof(note)` | MIDI to frequency | Returns frequency for MIDI note number |
| `fmap(in, min, max, curve)` | Value mapping | Maps 0-1 to min-max with curve (LINEAR, EXP, LOG) |
| `fclamp(val, min, max)` | Value clamping | Clamps value to range |
| `fonepole(out, in, coef)` | One-pole smoothing | In-place filter, coef = 1/(time * sr) |
| `fmax/fmin(a, b)` | Float max/min | Returns max/min of two floats |
| `TWOPI_F` | 2π constant | `6.28318530717958647692f` |
| `Metro` | Metronome | `Init(freq, sr)`, `SetFreq(freq)`, `Process()`, `Reset()` |
| `Port` | Portamento | `Init(sr, time)`, `Process(target)` |
| `MayTrig` | Probabilistic trigger | `Init()`, `SetProb(prob)`, `Process(trig_in)` |
| `SampleHold` | Sample and hold | `Init()`, `SetMode(mode)`, `Process(trig, in, mode)` |
| `Looper` | Audio looper | `Init(sr, mem, size)`, `SetMode(mode)`, `TrigRecord()`, `Process(in)`, `Clear()`, `IsRecording()` |

---

## Memory Management

### SDRAM (64MB external)
```cpp
// MUST be global scope, MUST use macro, NO constructors
float DSY_SDRAM_BSS delay_buffer[48000 * 10];  // 10 sec at 48kHz

int main() {
    hw.Init();
    // MUST initialize AFTER hw.Init() - contents are undefined at startup
    std::fill(delay_buffer, delay_buffer + 480000, 0.0f);
}
```

**SDRAM base address:** `0xC0000000` (if using pointers)

### DMA Buffers
```cpp
// Required for SPI/I2C DMA transfers - must be global
uint8_t DMA_BUFFER_MEM_SECTION spi_buffer[256];
```

### Memory Limits

| Region | Size | Notes |
|--------|------|-------|
| FLASH | 128KB | Program code (or 0 if using bootloader) |
| SRAM | 512KB | General RAM |
| DTCMRAM | 128KB | Fast RAM, used for BOOT_SRAM apps |
| SDRAM | 64MB | External, must use `DSY_SDRAM_BSS` |
| QSPIFLASH | ~8MB | For BOOT_QSPI apps |

---

## Audio Callback Rules

**NEVER in audio callback:**
- Blocking calls (SPI/I2C blocking transfers, `System::Delay`)
- Dynamic allocation (`new`, `malloc`)
- File I/O, printing

**Max callback time:** `(1 / sample_rate) * block_size`
Example: 48 samples at 48kHz = 1ms max

**Pattern for slow operations:**
```cpp
volatile bool do_save = false;

void AudioCallback(...) {
    if (hw.button1.RisingEdge()) do_save = true;
}

int main() {
    while(1) {
        if (do_save) {
            // Safe for slow operations here
            do_save = false;
        }
    }
}
```

---

## Debugging

### Serial Output
```cpp
hw.seed.StartLog();       // Non-blocking
hw.seed.StartLog(true);   // Wait for USB connection

hw.seed.PrintLine("val: %d", intVal);
hw.seed.PrintLine("flt: " FLT_FMT3, FLT_VAR3(floatVal));  // 3 decimal places
```

For `%f` support, add to Makefile: `LDFLAGS += -u _printf_float`

### CPU Load Monitoring
```cpp
CpuLoadMeter meter;
meter.Init(hw.AudioSampleRate(), hw.AudioBlockSize());

void AudioCallback(...) {
    meter.OnBlockStart();
    // ... processing
    meter.OnBlockEnd();
}

// In main loop:
float load = meter.GetAvgCpuLoad();  // 0.0-1.0
```

---

## Common Patterns

### Smoothed Parameter
```cpp
float target_freq = 440.0f;
float smooth_freq = 440.0f;

void AudioCallback(...) {
    target_freq = fmap(hw.knob1.Value(), 100.0f, 2000.0f, Mapping::LOG);
    
    for (size_t i = 0; i < size; i++) {
        fonepole(smooth_freq, target_freq, 0.001f);
        osc.SetFreq(smooth_freq);
        out[0][i] = osc.Process();
    }
}
```

### Encoder Menu Navigation
```cpp
enum State { PLAY, EDIT_FREQ, EDIT_AMP, NUM_STATES };
State state = PLAY;
float freq = 440.0f, amp = 0.5f;

void AudioCallback(...) {
    hw.ProcessAllControls();
    
    // Click cycles states
    if (hw.encoder.RisingEdge()) {
        state = static_cast<State>((state + 1) % NUM_STATES);
    }
    
    // Rotation adjusts current param
    int inc = hw.encoder.Increment();
    switch (state) {
        case EDIT_FREQ: freq = fclamp(freq + inc * 10.0f, 20.0f, 2000.0f); break;
        case EDIT_AMP: amp = fclamp(amp + inc * 0.05f, 0.0f, 1.0f); break;
        default: break;
    }
    
    // LED shows current state
    hw.led1.Set(state == EDIT_FREQ, state == EDIT_AMP, state == PLAY);
    hw.UpdateLeds();
}
```

### Polyphonic Voice Allocation
```cpp
struct Voice {
    Oscillator osc;
    Adsr env;
    uint8_t note;
    bool active;
};

Voice voices[8];

void NoteOn(uint8_t note, uint8_t vel) {
    for (auto& v : voices) {
        if (!v.active) {
            v.note = note;
            v.active = true;
            v.osc.SetFreq(mtof(note));
            v.osc.SetAmp(vel / 127.0f);
            v.env.Retrigger(false);
            return;
        }
    }
}

void NoteOff(uint8_t note) {
    for (auto& v : voices) {
        if (v.active && v.note == note) {
            v.active = false;  // env.Process(false) will handle release
        }
    }
}
```

---

## Code Style

Uses clang-format (version 10) with Allman braces:
- 4-space indentation, no tabs
- 80-column limit
- Braces on new lines
- Aligned consecutive assignments and declarations

---

## MIDI Controller Reference

For Arturia KeyLab Essential 61 mk2 MIDI mappings:
- Main Port: Keys (Ch 1), Pads (Ch 10, notes 36-43), Encoders (CCs 74,71,76,77,93,18,19,16,17), Faders (CCs 73,75,79,72,80,81,82,83,85)
- Mod wheel: CC 1, Sustain: CC 64, Pitch bend: 14-bit
- SysEx for LED/LCD control via DAW port
