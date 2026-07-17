# LilyFox

A fox-hunt beacon for the **LilyGO T-TWR Plus V2.0** (ESP32-S3 + SA868
VHF module with stock NiceRF firmware).

With `.wav` recordings on a micro-SD card, each transmission plays the
next recording in alphabetical rotation followed by a morse ID
(`de W2WZ`), ~30 seconds of transmission at the start of every minute
on 146.5650 MHz at low power (~0.5 W). Without a card it falls back to
a pure morse beacon (`fox fox fox fox de W2WZ` at 10 WPM).

## Voice recordings

Put one or more WAV files in the root of a micro-SD card:

- **Format**: mono PCM, 8- or 16-bit, any sample rate from 4 to 48 kHz
  (16 kHz is plenty for FM voice). From any recording, one ffmpeg line:
  `ffmpeg -i recording.m4a -ac 1 -ar 16000 fox.wav`
  or in Audacity: Tracks > Mix > Stereo to Mono, set project rate to
  16000, Export as WAV signed 16-bit PCM.
- **Level**: the firmware loudness-normalizes each recording as it
  loads (`VOICE_NORMALIZE`), compressing speech to a hot average level
  with a soft limiter. This is not just for loudness: the SA868's mic
  AGC amplifies its own hiss whenever the input is quiet, so keeping
  the drive high keeps the transmitted noise floor down. Avoid long
  stretches of dead air in recordings — the AGC hiss climbs back up
  within about half a second of silence.
- **Rotation**: files play in alphabetical order, one recording per
  cycle — name them `01-intro.wav`, `02-taunt.wav`, ... to control the
  order. If the TX window fits more than one play, the same recording
  repeats within that cycle.
- Files starting with `.` or `_` are ignored (macOS metadata).
- Each recording is loaded into PSRAM before the transmitter keys: SD
  card activity during transmission couples audible ticks into the TX
  audio, so the card is never touched while on the air. Recordings up
  to several minutes fit comfortably in the 8 MB PSRAM.

If a file is unreadable it is skipped; if the card disappears or every
file is bad, the fox falls back to the morse beacon rather than dying.

## How it works

- The AXP2101 PMU is brought up over I2C to enable the SA868's 3.4 V rail.
- The SA868 (stock NiceRF firmware) is programmed over UART with
  `AT+DMOSETGROUP` for simplex operation, 12.5 kHz bandwidth, no CTCSS.
- PTT is a GPIO (active low). TX audio (morse sine and voice PCM) is
  produced by the ESP32-S3's I2S peripheral in PDM TX mode — a DMA-fed
  hardware modulator on the SA868 mic input, clocked at each
  recording's native sample rate. (An earlier GPIO sigma-delta backend
  was measured 30 dB noisier — its in-band modulator hash, amplified by
  the radio's mic AGC, was the hiss floor — and was removed.)
- Only **complete** messages are sent: a repetition starts only if it fits
  in the remaining TX window, so the callsign is never cut off. At least
  one full message is sent per cycle even if it overruns the window.

Note: at 10 WPM the default message takes ~29.6 s, so each one-minute
cycle sends it exactly once, filling the whole 30 s window.

## Configuration

Everything is in [include/fox_config.h](include/fox_config.h):

| Setting | Default | Meaning |
|---|---|---|
| `FOX_CALLSIGN` | `W2WZ` | Station callsign, used in the beacon message, morse ID, and display |
| `MESSAGE` | `fox fox fox fox de <callsign>` | Morse beacon text (A–Z, 0–9, space, `. , ? / = -`) |
| `WORDS_PER_MINUTE` | `20` | Morse speed (PARIS standard) |
| `TX_WINDOW_SECONDS` | `30` | Transmit window at the start of each cycle |
| `PERIOD_SECONDS` | `60` | Full cycle length |
| `FREQUENCY_MHZ` | `146.5650` | Operating frequency, TX and RX (12.5/25 kHz multiple) |
| `TONE_HZ` | `800` | Audio tone frequency |
| `USE_HIGH_POWER` | `false` | `false` = low (~0.5 W), `true` = high |
| `SQUELCH_LEVEL` | `1` | Receiver squelch, 0–8 |
| `START_ENABLED` | `true` | Whether the beacon transmits right after boot |
| `VOICE_ENABLED` | `true` | Play SD-card recordings when present |
| `MORSE_ID` | `de <callsign>` | Morse identification after each recording |
| `VOICE_ID_GAP_MS` | `400` | Silence between recording and morse ID |
| `VOICE_GAIN` | `170` | Playback level (170 = full-scale WAV at max clean drive) |

CTCSS/CDCSS is always off (the `AT+DMOSETGROUP` CXCSS fields are fixed
at `0000`), per the NiceRF SA868S serial protocol.

Edit, then rebuild and reflash.

## Build and flash

Requires [PlatformIO](https://platformio.org/). With the board connected
over USB:

```sh
pio run -t upload
pio device monitor        # watch status output at 115200 baud
```

## Controls and display

Clicking the rotary encoder toggles the beacon on and off at any time.
A press during a transmission stops the morse within about half a
second and unkeys. Whether the beacon starts enabled after boot is the
`START_ENABLED` setting.

The OLED shows `Beacon Active` or `Beacon Inactive` plus a live
activity line: the recording being played (`voice: 01-intro.wav`), the
morse segment (`morse: de W2WZ` or `morse beacon`), or the countdown to
the next transmission (`next TX in 23s`). If no display is detected the
fox runs headless.

## Status LED

| Color | Meaning |
|---|---|
| Blue | Initializing |
| Dim green | Beacon active, idle between transmissions |
| Red | On the air |
| Off | Beacon inactive (toggled off) |
| Blinking red | Hardware fault — check the serial monitor |

## Troubleshooting boot problems

Watch the very first ROM line in the serial monitor after a reset:

- `boot:0x8` / `boot:0xa (SPI_FAST_FLASH_BOOT)` — normal application boot.
- `boot:0x0 (DOWNLOAD(USB/UART0))` + `waiting for download` — GPIO0 (the
  BOT button) was low at reset: the button is pressed, sticking, or
  shorted, and the app never runs (no LED, no output).
- `Last reset: BROWNOUT` in the firmware log — the supply sagged;
  charge or replace the battery.

The ESP32-S3's USB port re-enumerates on every reset, and the monitor
does not follow it — after any reset, quit (Ctrl+C) and rerun
`pio device monitor`.

Known artifact: long stretches of transmitted digital silence carry a
quiet rhythmic "motorboat" sound. Extensive measurement exonerated
every digital layer (DMA feed, underruns, ring geometry, descriptor
alignment, auto-clear, modulator dither, oversample ratio); injected
dither noise only masks it. It appears to originate in the radio's mic
path when fed a perfectly silent line. `VOICE_TRIM_PAUSES` keeps
silences short enough that it is not audible in practice. A future
experiment is the I2S PDM "DAC line mode" slot configuration
(`I2S_PDM_TX_SLOT_DAC_DEFAULT_CONFIG`), designed for RC-filter loads
like this board's.

Opening the monitor itself resets the chip (`rst:0x15
USB_UART_CHIP_RESET`): this is ESP32-S3 USB-Serial/JTAG hardware
behavior and cannot be fully suppressed without breaking auto-upload.
The fox simply reboots and starts a fresh cycle; the firmware
power-cycles the radio rail at every boot so the SA868 always starts
cold, even when the reset landed mid-transmission.

## Verifying

Listen on 146.565 MHz with an HT. On boot the serial monitor prints the
measured message duration and the schedule; each cycle logs when TX
starts and stops. If the SA868 never answers (`blinking red`,
`AT+DMOCONNECT` timeout in the log), the module may be running the
OpenRTX sa8x8 firmware instead of stock NiceRF — that needs a different
control protocol.

## Legal

Transmitting requires an amateur radio license, and the beacon message
must contain your callsign (the morse ID satisfies FCC Part 97
identification as long as each cycle transmits it). Check that the
frequency is appropriate for fox hunting in your area.
