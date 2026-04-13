# Changelog

All notable changes to this project will be documented in this file.

## [4.2.0-poe-wifi] - 2026-04-13

### Added
- **WiFi CSI: ESPectre port** — Hampel outlier filter (MAD-based, window=7)
- **WiFi CSI: Low-pass filter** — 1st-order Butterworth IIR at 11 Hz cutoff
- **WiFi CSI: CV normalization** — gain-invariant turbulence (std/mean) for ESP32 without AGC lock
- **WiFi CSI: DNS traffic generator** — FreeRTOS task sending UDP queries to gateway at 100 pps
- **WiFi CSI: Breathing-aware presence hold** — prevents dropping stationary person (~5 min max)
- **WiFi CSI: HT20/11n WiFi forcing** — consistent 64 subcarriers with guard-band-aware selection
- **WiFi CSI: STBC packet handling** — collapsed doubled packets (256→128 bytes)
- **WiFi CSI: Short HT20 handling** — 114-byte packets remapped with left guard padding
- **WiFi CSI: CSI packet length validation** — rejects non-standard packets
- **Radar: Entry/exit path validation** — zone `valid_prev_zone` field, invalid path → immediate trigger
- **API: Event timeline** — `current_zone`, `debounce_frames`, `last_event` in `/api/alarm/status`
- **API: Debounce frames** — configurable via `/api/alarm/config` POST

### Changed
- Radar processing tick: 1s → **50ms** (20 Hz) to catch short detections
- MQTT TIER 1 state changes: lastPub cache updated only on successful publish
- CSI subcarriers: `{6,10,...}` → `{12,14,16,18,20,24,28,36,40,44,48,52}` (out of guard bands)
- CSI temporal smoothing: 3/6 enter → **4/6** (matches ESPectre MVS)
- CSI idle amplitude baseline: placeholder → real amplitude sum
- CSI two-pass variance for per-packet turbulence (numerically stable on float32)

## [4.1.4-poe-wifi] - 2026-04-12

### Added
- WiFi CSI runtime configuration via REST API and GUI
- CSI tab in web dashboard with live sparkline graph
- Auto-calibration, idle baseline reset, WiFi reconnect actions
- CSI metrics in SSE telemetry stream

## [4.1.3-poe-wifi] - 2026-04-10

### Changed
- Swapped me-no-dev/AsyncTCP + ESPAsyncWebServer for ESP32Async community fork
- Fixes race conditions in digest auth parser and TCP close handling

## [4.1.2-poe-wifi] - 2026-04-09

### Fixed
- SSE live telemetry buffer regression from LD2412 v3.10.0 port

## [4.1.1-poe-wifi] - 2026-04-08

### Changed
- Security hardening from ESPHome 2026.3 community audit

## [4.1.0-poe-wifi] - 2026-04-05

### Added
- Static IP configuration
- Scheduled arm/disarm with timezone support
- CSV event export
- Auto-arm after configurable idle period
- Heap optimizations

## [4.0.6-poe-wifi] - 2026-03-28

### Added
- Heap diagnostics with crash guards and bounds validation

## [4.0.5-poe-wifi] - 2026-03-27

### Added
- Telegram alerts for low RAM (warn/critical/recover thresholds)

## [4.0.4-poe-wifi] - 2026-03-26

### Added
- Telegram alerts for chip temperature

## [4.0.3-poe-wifi] - 2026-03-25

### Added
- Chip temperature MQTT publishing with configurable interval

## [4.0.2-poe-wifi] - 2026-03-24

### Added
- OTA rollback bootloader
- Chip temperature monitoring

## [4.0.0-poe-wifi] - 2026-03-22

### Added
- LAN8720A PHY LED control via MDIO

## [3.9.9-poe-wifi] - 2026-03-21

### Added
- Web assets on LittleFS with PROGMEM fallback

## [3.9.8-poe-wifi] - 2026-03-20

### Added
- MQTT offline buffer: queue messages to LittleFS when disconnected

## [3.9.5-poe-wifi] - 2026-03-18

### Added
- Initial WiFi CSI implementation (basic turbulence, phase, ratio, breathing)
- WiFi STA mode alongside Ethernet for CSI-only capture
