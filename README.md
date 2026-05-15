# Flood Sensor — nRF9151 Firmware

Firmware for the Circuit Dojo Feather nRF9151 that wakes a SparkFun XM125 60GHz radar sensor, collects distance measurements, and transmits them over LTE-M to a Google Apps Script endpoint.

## System Overview

```
nRF9151 Feather (master)
  │
  ├─ WAKE (P0.20) ──────────────────► XM125 WU pin
  ├─ INT  (P0.21) ◄──────────────────  XM125 MCU_INT pin
  └─ UART RX (P0.23) ◄───────────────  XM125 UART TX
                                        (921600 baud)
```

**Measurement cycle (every 60 seconds):**

1. nRF9151 asserts WAKE → XM125 wakes from hibernate
2. XM125 takes 7 measurements, sends each over UART
3. XM125 asserts INT → nRF9151 wakes from eDRX idle
4. nRF9151 reads 7 frames from UART ring buffer
5. nRF9151 deasserts WAKE → XM125 hibernates
6. nRF9151 timestamps frames (SNTP-anchored) and POSTs over LTE-M
7. nRF9151 returns to eDRX idle until next cycle

## Hardware

- **Cellular modem**: Circuit Dojo Feather nRF9151
- **Radar sensor**: SparkFun XM125 (Acconeer A121, 60GHz)
- **SIM**: Hologram (LTE-M, APN: `hologram`)

## Wiring

| XM125 Pin       | nRF9151 Pin | Direction   | Purpose             |
| --------------- | ----------- | ----------- | ------------------- |
| UART TX         | P0.23       | XM125 → nRF | Measurement data    |
| WU (cut jumper) | P0.20       | nRF → XM125 | Wake from hibernate |
| MCU_INT         | P0.21       | XM125 → nRF | Buffer ready signal |
| 3V3             | 3V3         | nRF → XM125 | Power               |
| GND             | GND         | —           | Common ground       |

> **Note:** The WU jumper on the XM125 SparkFun breakout must be cut before connecting WU to P0.20. Otherwise the pin is held high by the board's default pull-up and the XM125 will never hibernate.

> **Boot safety:** P0.20 is driven low via a devicetree gpio-hog before `main()` runs, preventing the XM125 from waking due to a floating signal during nRF9151 startup. No external pull-down resistor is required.

The XM125 is powered from the nRF9151 Feather's 3V3 rail. The nRF9151 is powered via USB during development or via LiPo battery with solar charging in the field.

## Prerequisites

- [Circuit Dojo Zephyr Tools](https://github.com/circuitdojo/nrf9160-feather-examples-and-drivers) installed
- NCS v3.2.1 workspace initialized via `west`
- `probe-rs` installed for flashing
- ARM GNU Toolchain and CMake on PATH

## Building

From the `webhook_test` folder, activate the environment:

```powershell
# Windows
C:\Users\<user>\.zephyrtools\env\Scripts\Activate.ps1
$env:PATH = "C:\Users\<user>\.zephyrtools\cmake\cmake-3.22.0-windows-x86_64\bin;" + $env:PATH
$env:ZEPHYR_BASE = "C:\p\n\zephyr"
```

```bash
# macOS
source ~/.zephyrtools/env/bin/activate
```

Then build:

```powershell
west build -b circuitdojo_feather_nrf9151/nrf9151/ns --pristine
```

## Flashing

```powershell
probe-rs download --chip nRF9151_xxAA --binary-format hex build/merged.hex
```

Press the RST button after flashing.

## Configuration

All key parameters are in `src/config.h`:

| Parameter               | Default               | Description                                 |
| ----------------------- | --------------------- | ------------------------------------------- |
| `UPDATE_RATE_MS`        | `60000`               | Measurement cycle interval (ms)             |
| `FRAMES_PER_BUFFER`     | `7`                   | Frames per LTE POST (must be multiple of 7) |
| `MAX_PEAKS_PER_FRAME`   | `3`                   | Max peaks reported per frame                |
| `UART_RX_BUF_SIZE`      | `512`                 | UART ring buffer size (bytes)               |
| `UART_FRAME_TIMEOUT_MS` | `5000`                | Timeout waiting for XM125 INT (ms)          |
| `SNTP_SERVER`           | `time.cloudflare.com` | SNTP server for time sync                   |

### HTTP Endpoint

Edit `src/config.h`:

```c
#define WEBHOOK_HOST  "script.google.com"
#define WEBHOOK_UUID  "macros/s/YOUR_SCRIPT_ID/exec"
```

### TLS Certificate

The Google GTS root CA is in `cert/google_gts.pem`. Replace with the appropriate root CA if targeting a different endpoint.

### Network Operator

LTE auto-selects by default. If auto-selection fails with the Hologram SIM, force T-Mobile by uncommenting in `src/cellular.c`:

```c
nrf_modem_at_printf("AT+COPS=1,2,\"310260\",7");  // 310260 = T-Mobile US
```

## Data Format

Each HTTP POST sends a JSON body with `FRAMES_PER_BUFFER` frames. Fields with no detected peaks omit distance and strength:

```json
{
  "frames": [
    {
      "datetime": 1747123456789,
      "frame": 1,
      "num_peaks": 2,
      "d1": 1.234567,
      "s1": 45.2,
      "d2": 0.987654,
      "s2": 38.1
    },
    {
      "datetime": 1747123457000,
      "frame": 2,
      "num_peaks": 0
    }
  ]
}
```

| Field       | Description                                    |
| ----------- | ---------------------------------------------- |
| `datetime`  | Unix timestamp in milliseconds (SNTP-anchored) |
| `frame`     | Frame number within buffer (1–7)               |
| `num_peaks` | Number of detected peaks (0 = no detection)    |
| `d1`–`d3`   | Distance to peak in meters (up to 3 peaks)     |
| `s1`–`s3`   | Signal strength in dB (up to 3 peaks)          |

### Google Sheet Columns

The Google Apps Script writes each frame as one row:

`Timestamp | Datetime | Frame | Num Peaks | Distance 1 | Strength 1 | Distance 2 | Strength 2 | Distance 3 | Strength 3`

## Power

- **Sleep mode**: eDRX (20.48s cycle requested, network-granted)
- **Solar**: 2×2" panel recommended for continuous outdoor operation
- **Update rate**: 1 minute default — adjust `UPDATE_RATE_MS` in `config.h`

> If `UPDATE_RATE_MS` is increased beyond 5 minutes, consider enabling PSM in `cellular.c` for better power efficiency.

## XM125 Firmware

The XM125 runs a modified version of Acconeer's `example_detector_distance_low_power_hibernate` example from the A121 SDK. Key changes from the original:

- XM125 is slave — sleeps in hibernate until WAKE pin asserted by nRF9151
- Takes `FRAMES_PER_BUFFER` measurements per wake cycle
- Outputs one UART line per frame: `distance,strength [distance,strength ...]\n` or `0\n` for no detection
- Asserts MCU_INT after last frame is sent
- Waits for WAKE deassert before returning to hibernate

See the `xm125` repository for that firmware.

## Serial Monitor

Connect at **115200 baud** to view nRF9151 debug output (Zephyr logging).
