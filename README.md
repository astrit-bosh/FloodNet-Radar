# Field Radar — nRF9151 Firmware

Firmware for the Circuit Dojo Feather nRF9151 that reads distance measurements from a SparkFun XM125 60GHz radar sensor and transmits them over LTE-M to an HTTP endpoint.

## System Overview

```
XM125 (60GHz Radar) → UART @ 921600 baud → nRF9151 Feather → LTE-M → HTTP POST
```

The XM125 runs Acconeer's low-power hibernate firmware, waking every ~8.5 seconds to take a measurement and output a JSON frame over UART. The nRF9151 buffers 7 frames (~1 minute of data), then connects to LTE-M and POSTs all frames to a configurable HTTP endpoint.

## Hardware

- **Cellular modem**: Circuit Dojo Feather nRF9151
- **Radar sensor**: SparkFun XM125 (Acconeer A121, 60GHz)
- **SIM**: Hologram (LTE-M, APN: `hologram`)

## Wiring

| XM125 Pin | Feather nRF9151 Pin |
|-----------|---------------------|
| TX        | P0.23 (RX / uart1)  |
| GND       | GND                 |

Both boards are powered independently via USB or battery.

## Prerequisites

- [Circuit Dojo Zephyr Tools](https://github.com/circuitdojo/nrf9151-feather-ncs) installed
- NCS v3.2.1 workspace initialized via `west`
- `probe-rs` installed for flashing
- ARM GNU Toolchain and CMake on PATH

## Building

From the workspace root (e.g. `C:\p\n\`), activate the environment and set required paths:

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

Then build from the `webhook_test` folder:

```bash
west build -b circuitdojo_feather_nrf9151/nrf9151/ns -d build/circuitdojo_feather_nrf9151 --sysbuild
```

## Flashing

```bash
probe-rs download --chip nRF9151_xxAA --binary-format hex build/circuitdojo_feather_nrf9151/merged.hex
```

Press the RST button after flashing.

## Configuration

### HTTP Endpoint

Edit `src/cellular.c` to set your endpoint:

```c
#define WEBHOOK_HOST "webhook.site"
#define WEBHOOK_UUID "your-uuid-here"
```

### Frame Buffer Size

Edit `src/radar.h` to change how many frames are buffered before sending:

```c
#define RADAR_FRAME_BUF_COUNT 7  /* ~1 minute at 7 frames/min */
```

### TLS Certificate

The TLS certificate for your HTTPS endpoint lives in `cert/DigiCertGlobalG3.pem`. Replace with the appropriate root CA for your endpoint.

### Network Operator

If LTE auto-selection fails (common with Hologram SIM), the firmware forces T-Mobile via AT command in `cellular_connect()`. Edit `src/cellular.c` if a different carrier is needed:

```c
nrf_modem_at_printf("AT+COPS=1,2,\"310260\",7");  /* 310260 = T-Mobile US */
```

## Data Format

Each HTTP POST sends a JSON body with 7 frames:

```json
{
  "frames": [
    {
      "t": 7955,
      "n": 1,
      "peaks": [
        { "d": 0.4317, "s": -14.55 }
      ]
    }
  ]
}
```

| Field | Description |
|-------|-------------|
| `t`   | Timestamp (ms since nRF boot) |
| `n`   | Number of detected peaks |
| `d`   | Distance to peak (meters) |
| `s`   | Signal strength (dB) |

## XM125 Firmware

The XM125 runs a modified version of Acconeer's `example_detector_distance_low_power_hibernate` example. Key changes:

- Update rate set to `0.1167 Hz` (7 per minute)
- `print_result()` outputs JSON instead of plain text
- Build with ARM GCC 13.x and STM32Cube FW L4

See the `xm125` folder for that firmware (separate repository).

## Serial Monitor

Connect at **115200 baud** to view nRF9151 debug output. Connect at **921600 baud** to view raw XM125 JSON output.

## TO-DO

Refactor to multi-threading:

┌─────────────────────────────────────────────────────┐  
  ### RADAR THREAD (always running)                      
                                                       
  Wait on UART semaphore (sleep until XM125 sends)     \
  Parse frame                                          \
  Update rolling baseline                               \
  Check flood detection:                                \
    - delta > threshold for N consecutive frames?       \
    - Yes → set flood_event flag, include IQ           \
    - No  → clear consecutive counter                   \
  Write frame to ring buffer (mutex protected)          \
  Signal cellular thread if buffer ready                \
└─────────────────────────────────────────────────────┘  
             ↓ ring buffer (mutex)  
           
┌─────────────────────────────────────────────────────┐  
 ### CELLULAR THREAD (wakes periodically)              
                                                      
  Wait on signal from radar thread                    \
  Connect LTE once                                    \
  POST frames:                                        \
    - Always include peaks                            \
    - Include IQ only if flood_event active           \
  Disconnect LTE                                      \
  Sleep until next batch ready                        \
└─────────────────────────────────────────────────────┘  
Clean up codebase \
Test flood detection \
Test power usage \
Test cellular data usage \
Local "field" test for 1 week \
Maxbotix co-located deployment for true field test
