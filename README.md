# POE-2412-WiFi-CSI Security

**LD2412 mmWave radar security system + WiFi CSI motion detection** on a PoE-powered ESP32 board.

Combines HLK-LD2412 24GHz radar with WiFi Channel State Information (CSI) analysis for dual-sensor presence and intrusion detection. Ethernet (PoE) handles all network connectivity while WiFi runs purely as a passive CSI sensor.

> **Based on [ESPectre](https://github.com/francescopace/espectre)** by Francesco Pace (GPLv3) — WiFi CSI detection algorithms, filters, and traffic generation concepts ported from ESPHome to Arduino/PlatformIO standalone.

## Features

### Radar (HLK-LD2412)
- Real-time presence/motion detection with configurable zones
- Entry/exit path validation (zone sequencing for alarm behavior)
- Alarm state machine: DISARMED → ARMING → ARMED → PENDING → TRIGGERED
- Anti-masking (blind detection), loitering alerts, pet immunity
- 14-gate sensitivity control with engineering mode
- Static reflector learning (auto-zone suggestion)

### WiFi CSI (Channel State Information)
- Passive motion detection via WiFi signal perturbation analysis
- 12 HT20 subcarriers (guard-band-aware selection)
- Spatial turbulence with CV normalization (gain-invariant)
- Phase turbulence, ratio turbulence (SA-WiSense), breathing score
- Hampel outlier filter + low-pass Butterworth IIR
- Breathing-aware presence hold (prevents dropping stationary person)
- DNS traffic generator (100 pps to gateway for consistent CSI rate)
- STBC and short HT20 packet handling
- Runtime threshold/window/hysteresis configuration via GUI

### Security & Notifications
- MQTT integration with Home Assistant auto-discovery
- Telegram bot (arm/disarm, status, alerts, mute)
- Scheduled arm/disarm with timezone support
- Auto-arm after configurable idle period
- Event log with CSV export and paginated API
- Supervision heartbeat and mesh peer verification
- MQTT offline buffer (LittleFS-backed queue)

### System
- OTA firmware update with rollback support
- Config snapshot backup before OTA
- LittleFS web asset serving (hot-swap GUI)
- Static IP configuration
- Chip temperature and heap monitoring with Telegram alerts
- Factory reset via GPIO 0 long press

## Hardware

**Board:** Prokyber ESP32-STICK active PoE (LAN8720A RMII Ethernet)

| Pin | Function | Note |
|-----|----------|------|
| GPIO0 | ETH CLK IN | EMAC_TX_CLK (LAN8720→ESP32) |
| GPIO2 | USER LED | Status LED |
| GPIO4 | RADAR_OUT_PIN | Digital presence output |
| GPIO18-27 | RESERVED | LAN8720A Ethernet (do not use!) |
| GPIO32 | RADAR_RX | ← LD2412 TX |
| GPIO33 | RADAR_TX | → LD2412 RX |

**WARNING:** GPIO 18, 19, 21, 22, 23, 25, 26, 27 are reserved for Ethernet — cannot be used!

## Build & Flash

### Prerequisites
- [PlatformIO](https://platformio.org/) CLI or IDE

### First flash (USB cable)
```bash
# Without WiFi CSI
pio run -e esp32_poe --target upload

# With WiFi CSI sensor
pio run -e esp32_poe_csi --target upload
```

### OTA update
1. Set your device IP in `platformio.ini` → `[env:ota_poe_dev]` → `upload_port`
2. `pio run -e ota_poe_dev --target upload` (or `ota_poe_csi` for CSI variant)

## Configuration

### Secrets
Edit `include/secrets.h`:
- `MQTT_SERVER_DEFAULT` — MQTT broker IP
- `MQTT_USER_DEFAULT` / `MQTT_PASS_DEFAULT` — MQTT credentials
- `CSI_WIFI_SSID` / `CSI_WIFI_PASS` — WiFi network for CSI capture (CSI variant only)

### Device Registration
Add your device to `include/known_devices.h` after first flash:
```cpp
{ "xx:xx:xx:xx:xx:xx", "poe2412_room", "poe2412-room" },
```
Find the MAC address in serial console at boot: `[SETUP] MAC: xx:xx:...`

## API Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/health` | Uptime, Ethernet info, MQTT, heap, reset history |
| GET | `/api/telemetry` | Radar state, distance, energy, UART stats |
| GET/POST | `/api/config` | System configuration |
| GET/POST | `/api/alarm/status` | Alarm state, zones, last event |
| POST | `/api/alarm/arm` | Arm system (optional `?immediate=1`) |
| POST | `/api/alarm/disarm` | Disarm system |
| GET/POST | `/api/security/config` | Anti-masking, loitering, heartbeat |
| GET/POST | `/api/schedule` | Scheduled arm/disarm times |
| GET/POST | `/api/timezone` | Timezone offset |
| GET | `/api/events/csv` | Download events as CSV |
| GET/POST | `/api/csi` | CSI config and live metrics (CSI variant) |
| POST | `/api/csi/calibrate` | Auto-calibrate CSI threshold |

### Differences from base LD2412
- `/api/health` → `ethernet.link_up`, `ethernet.ip`, `ethernet.mac`, `ethernet.speed` (instead of `wifi.rssi`, `wifi.ssid`)
- STAB log: `ETH:UP/DOWN` instead of RSSI value
- SSE telemetry: `eth_link` instead of `rssi`

## Build Environments

| Environment | Description |
|-------------|-------------|
| `esp32_poe` | Radar only (no WiFi CSI) |
| `esp32_poe_csi` | Radar + WiFi CSI fusion |
| `ota_poe_dev` | OTA upload (radar only) |
| `ota_poe_csi` | OTA upload (radar + CSI) |

## Related Projects

- **[HLK-LD2412-security](https://github.com/PeterkoCZ91/HLK-LD2412-security)** — Base WiFi version for standard ESP32 boards
- **[POE-2412-security](https://github.com/PeterkoCZ91/POE-2412-security)** — PoE Ethernet version (radar only, no CSI)
- **[ESPectre](https://github.com/francescopace/espectre)** — WiFi CSI motion detection (ESPHome component) by Francesco Pace

## License

This project is licensed under the **GNU General Public License v3.0** — see [LICENSE](LICENSE) for details.

WiFi CSI detection algorithms are based on [ESPectre](https://github.com/francescopace/espectre) by Francesco Pace, licensed under GPLv3.
