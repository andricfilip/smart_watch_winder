# Watch Winder - Full Project Guide

![Watch Winder](docs/images/watch_winder.jpg)

This repository contains the complete watch-winder solution:

1. ESP8266 firmware for one watch (`winder/winder.ino`)
2. ESP8266 firmware for two watches (`doubleWinder/doubleWinder.ino`)
3. Raspberry Pi web dashboard (`pi-dashboard/`)
4. Modern React dashboard (`pi-dashboard-modern/`)
5. Assembly docs (SR + EN) in `docs/`

The firmware is API-only (no embedded HTML UI), and the dashboard controls the device over HTTP.

## 1) Project Structure

- `winder/winder.ino` - single watch firmware
- `doubleWinder/doubleWinder.ino` - double watch firmware
- `pi-dashboard/index.html` - dashboard UI
- `pi-dashboard/app.js` - dashboard logic and API calls
- `pi-dashboard/style.css` - dashboard style
- `pi-dashboard/docker-compose.yml` - dashboard container deploy
- `pi-dashboard-modern/` - modern React + Vite dashboard
- `docs/ASSEMBLY_GUIDE_SR.md` - assembly guide in Serbian
- `docs/ASSEMBLY_GUIDE_EN.md` - assembly guide in English
- `docs/images/` - wiring images used in docs
- `docs/images/watch_winder.jpg` - project preview image

## Assembly Guides

- Serbian: [docs/ASSEMBLY_GUIDE_SR.md](docs/ASSEMBLY_GUIDE_SR.md)
- English: [docs/ASSEMBLY_GUIDE_EN.md](docs/ASSEMBLY_GUIDE_EN.md)

## 2) How The System Works

1. ESP8266 runs motor control and exposes HTTP API.
2. Raspberry Pi hosts the dashboard.
3. Browser dashboard sends commands directly to ESP8266 API.
4. Internet is not required.
5. Local network path between browser and ESP8266 is required.

## 3) Choose Firmware (Single vs Double)

Flash only one firmware on one ESP8266 board:

- Use `winder/winder.ino` for one watch.
- Use `doubleWinder/doubleWinder.ino` for two watches.

## 4) Firmware Configuration (Required)

Before flashing, edit these constants in the selected `.ino` file:

- `STA_SSID`
- `STA_PASSWORD`
- `ENABLE_AP_FALLBACK`
- `AP_SSID`
- `AP_PASSWORD`
- `STA_CONNECT_TIMEOUT_MS`

Recommended values:

- `ENABLE_AP_FALLBACK = true` while testing
- `AP_PASSWORD` at least 12 characters
- `STA_CONNECT_TIMEOUT_MS = 10000` to `20000`

What they mean:

- `STA_*` = your home/local Wi-Fi credentials
- `ENABLE_AP_FALLBACK`:
  - `true` -> if STA connection fails, ESP starts fallback AP
  - `false` -> no AP fallback, ESP stays without network if STA fails
- `AP_*` = fallback network name/password

## 5) Runtime Config Parameters (TPD Engine)

The firmware now uses a time-based TPD scheduler instead of fixed `giri/delay` loops.

### Core parameters

- `targetTPD` range: `500-1000` (hard cap)
- `direction`: `CW`, `CCW`, `BIDIR`
- `mode`: `STANDARD`, `SMART`
- `speed` range: `1-5`
- `activeHours`: `[startHour, endHour]` (0-23)
- `runMinutes` range: `0-1440`

### Optional quiet profile

- `quietMode`: `true/false`
- `quietStartHour`, `quietEndHour`
- `nightSpeedCap`

Timer behavior:

- `runMinutes = 0` -> unlimited run time
- `runMinutes > 0` -> auto-stop after timer expires

## 6) API Endpoints

Available in both firmware variants:

- `GET /api/status`
- `GET /api/stats`
- `GET /api/health`
- `POST /api/config`
- `POST /api/start`
- `POST /api/stop`

Notes:

- Config write is POST-only.
- `POST /api/stop` is fail-safe and immediately releases motor outputs.
- Legacy `giri/delay` keys are still accepted for backward compatibility.

### Example `/api/config` payload

```json
{
  "targetTPD": 650,
  "direction": "BIDIR",
  "mode": "SMART",
  "speed": 3,
  "activeHours": [8, 23],
  "runMinutes": 0,
  "quietMode": true,
  "quietStartHour": 22,
  "quietEndHour": 7,
  "nightSpeedCap": 2
}
```

## 7) EEPROM Safety

Both firmware variants use protected config storage:

- magic
- version
- CRC

This prevents invalid config loads when format changes or EEPROM data is corrupted.

## 8) Stability Enhancements In Firmware

Both firmware variants include:

- non-blocking `millis()` scheduler (Wi-Fi friendly)
- rolling 24h progress tracking with hourly drift correction
- adaptive burst/pause distribution based on drift percent
- soft start/stop step ramp to reduce vibration/noise
- state machine: `IDLE`, `RUNNING`, `PAUSED`, `ERROR`, `TIMER_DONE`
- robust persistence with versioned records, CRC, and dual-slot write strategy
- periodic runtime snapshot for power-loss recovery

New runtime metrics exposed via API:

- `rotationsToday`
- `rotationsLastHour`
- `currentTPD`
- `driftPercent`
- `currentTPDProgress`
- `remainingRotations`
- `errorCode`

## 9) Flashing ESP8266

Use Arduino IDE or PlatformIO.

Basic Arduino IDE flow:

1. Open selected `.ino`.
2. Select correct ESP8266 board and COM port.
3. Install required libraries if missing (`ESP8266WiFi`, `ESP8266WebServer`, `EEPROM`).
4. Verify/Compile.
5. Upload.
6. Open Serial Monitor (115200) and read assigned IP.

## 10) Dashboard Setup On Raspberry Pi

### Option A - Simple static server

```bash
cd pi-dashboard
python3 -m http.server 8080
```

Open:

- Local Pi browser: `http://localhost:8080`
- Other LAN devices: `http://<IP_RASPBERRY_PI>:8080`

### Option B - Docker

```bash
cd pi-dashboard
docker compose up -d --build
```

Open from LAN devices:

- `http://<IP_RASPBERRY_PI>:8080`

Stop:

```bash
docker compose down
```

## 11) Dashboard First Start

1. Open dashboard.
2. Enter ESP URL (default example: `http://192.168.1.111`).
3. Choose mode:
   - `Single winder (1 sat)`
   - `Double winder (2 sata)`
4. Save device.
5. Click `Osvezi status`.
6. Configure values.
7. Click `Start`.
8. Use `Stop` when needed.

## 12) Network Scenarios

### Scenario A: Home Wi-Fi works

- ESP connects as STA.
- Dashboard (Pi/phone/laptop) reaches ESP via local IP.

### Scenario B: Home Wi-Fi fails + fallback enabled

- ESP starts fallback AP.
- Connect client device to AP.
- Dashboard works over fallback AP network.

### Scenario C: Home Wi-Fi fails + fallback disabled

- ESP is unreachable over network.
- Dashboard UI opens, but API calls fail.

## 13) Security Notes

- Keep strong `AP_PASSWORD` if fallback is enabled.
- Do not expose dashboard to internet unless you add auth/reverse proxy hardening.
- If you want LAN-only access, do not configure router port forwarding for `8080`.

## 14) Quick Troubleshooting

If dashboard shows offline / fetch errors:

1. Verify ESP and dashboard client are on reachable networks.
2. Confirm ESP IP in dashboard URL field.
3. Open `http://<ESP_IP>/api/status` directly from browser.
4. Check Serial Monitor for STA success/failure and fallback AP startup.
5. If browser cached old URL, save the new URL in dashboard again.

## 15) Recommended Production Profile

1. Validate stable STA connection for several reboots.
2. Keep fallback AP enabled only if you need service recovery.
3. Set sensible timer and speed values for your watch movement.
4. Periodically inspect `cyclesCompleted` and `lastStopReason` from `/api/status`.
