# Application User Guide (Modern Dashboard)

This is a practical guide: what to configure, which values to use, and what result you get on your watch winder.

## 1. What this app does

The app controls ESP8266 firmware over local network.

Supported:

1. start and stop winding
2. TPD-based configuration
3. direction and mode selection
4. runtime and health telemetry

## 2. Quick start (3 minutes)

1. Make sure firmware is uploaded to ESP:
   - single: `winder/winder.ino`
   - double: `doubleWinder/doubleWinder.ino`
2. Start dashboard:
   - `cd pi-dashboard-modern`
   - `docker compose up --build -d`
3. Open in browser: `http://IP_RPI:8090` (or `http://localhost:8090` on Raspberry Pi)
4. Click `Refresh`.
5. Set parameters and click `Save settings`.
6. Click `Start`.

If status is `Offline`, verify ESP and dashboard are on same network and device URL is correct.

## 3. Parameters and expected behavior

## targetTPD (500-1000)

Meaning:

- number of target rotations in 24h

Effect:

- higher TPD = more frequent activity and more total rotations
- lower TPD = less frequent activity and quieter operation

Recommendation:

- start with `650`, then fine tune for your watch

## direction (CW / CCW / BIDIR)

Meaning:

- winding direction

Effect:

- `CW`: clockwise only
- `CCW`: counterclockwise only
- `BIDIR`: alternating direction (safe default if watch requirement is unknown)

## mode (STANDARD / SMART)

Meaning:

- cycle distribution strategy

Effect:

- `STANDARD`: regular distribution through the day
- `SMART`: adaptive balancing of winding cycles

## speed (1-5)

Meaning:

- stepper speed level

Effect:

- `1-2`: quieter and smoother
- `4-5`: faster execution, potentially more noise

Recommendation:

- use `2` or `3` for stable and quiet behavior

## activeHours

Meaning:

- time window when winding is allowed

Effect:

- outside this range, system pauses

Example:

- `8-23`: active during day/evening, idle at night

## smartSwitchRotations

Meaning:

- in SMART mode, direction switch interval in rotations

Effect:

- higher value = less frequent direction changes
- lower value = more frequent direction changes

## quietMode + quietStartHour + quietEndHour + nightSpeedCap

Meaning:

- quieter night profile

Effect:

- in quiet hours speed is limited by `nightSpeedCap`
- less noise during night

Example:

- quiet from `22` to `7`, cap `2`

## runMinutes

Meaning:

- max runtime before auto-stop

Effect:

- `0` = no time limit
- `>0` = auto-stop after configured duration

## 4. Status panel metrics

1. `State`: current state (`IDLE`, `RUNNING`, `PAUSED`, `ERROR`, `TIMER_DONE`)
2. `Error`: error code if present
3. `Timer`: remaining timer time
4. `Network`: network mode and IP
5. `Rot today`: total rotations today
6. `Rot hour`: rotations in last hour
7. `Current TPD`: projected current TPD pace
8. `Drift %`: deviation from planned daily pace
9. `Progress`: completion percent for daily target
10. `Remaining`: rotations left to target
11. `Heap`: free ESP memory
12. `Uptime`: device uptime

## 5. Recommended starting values

### Single

1. targetTPD: `650`
2. direction: `BIDIR`
3. mode: `STANDARD`
4. speed: `2`
5. activeHours: `0-23`
6. quietMode: `true`
7. quiet: `22-7`, cap `2`

### Double

1. targetTPD1/2: `650`
2. direction1/2: `BIDIR`
3. mode1/2: `STANDARD`
4. speed1/2: `2`
5. active hours for both heads: `0-23`
6. quietMode: `true`

## 6. Single vs Double

1. If firmware is single, use single settings.
2. If firmware is double, use double settings.
3. If mode lock is enabled via Docker env, app mode cannot be changed from UI.

## 7. Common situations and fixes

## Offline

1. Check `APP_DEFAULT_DEVICE_URL` in `docker-compose.yml`.
2. Verify ESP responds to `GET /api/status` from browser.
3. Confirm ESP and dashboard are on same LAN.

## Motor is too noisy

1. lower `speed` to `2`
2. enable `quietMode`
3. set `nightSpeedCap` to `1` or `2`

## Watch seems under-wound

1. increase `targetTPD` in +50 steps
2. monitor `Drift %` and `Remaining`

## 8. Docker startup config (important)

In `pi-dashboard-modern/docker-compose.yml` use:

1. `APP_DEVICE_MODE`: `single` or `double`
2. `APP_LOCK_DEVICE_MODE`: lock mode in UI
3. `APP_DEFAULT_DEVICE_URL`: ESP IP address
4. `APP_LOCK_DEVICE_URL`: lock URL field
5. `APP_LANGUAGE`: `sr` or `en`
6. `APP_LOCK_LANGUAGE`: lock language selection

After changing env values, run:

1. `docker compose up --build -d`
