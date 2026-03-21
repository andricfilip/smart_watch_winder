# Kontrola navijanja automatskog sata

Ovaj projekat ima dva firmware fajla i jedan dashboard:

1. `winder/winder.ino` za jedan sat.
2. `doubleWinder/doubleWinder.ino` za dva sata.
3. `pi-dashboard/` web panel za upravljanje i telemetriju.

## API endpointi

- `GET /api/status`
- `GET /api/stats`
- `GET /api/health`
- `POST /api/config`
- `POST /api/start`
- `POST /api/stop`

## Sta da menjas u firmware fajlovima

U oba fajla menjas iste mrezne konstante na vrhu:

1. `STA_SSID`: naziv tvoje kućne Wi-Fi mreze.
2. `STA_PASSWORD`: lozinka tvoje kućne mreze.
3. `ENABLE_AP_FALLBACK`: `true` ili `false`.
4. `AP_SSID`: naziv fallback AP mreze.
5. `AP_PASSWORD`: lozinka fallback AP mreze.
6. `STA_CONNECT_TIMEOUT_MS`: vreme cekanja konekcije na Wi-Fi.

Preporucene vrednosti:

1. `ENABLE_AP_FALLBACK = true` dok testiras.
2. `AP_PASSWORD` promeni u jaku lozinku od bar 12 karaktera.
3. `STA_CONNECT_TIMEOUT_MS = 10000` do `20000`.

## Razlika single i double firmware-a (TPD engine)

Single (`winder/winder.ino`) glavni parametri:

1. `targetTPD` opseg `500-1000`.
2. `direction`: `CW`, `CCW`, `BIDIR`.
3. `mode`: `STANDARD` ili `SMART`.
4. `speed` opseg `1-5`.
5. `activeHours`: `[startHour, endHour]`.
6. `runMinutes` opseg `0-1440`.

Double (`doubleWinder/doubleWinder.ino`) glavni parametri:

1. `targetTPD1`, `targetTPD2` opseg `500-1000`.
2. `direction1`, `direction2`.
3. `mode1`, `mode2`.
4. `speed1`, `speed2` opseg `1-5`.
5. `activeStartHour1/2`, `activeEndHour1/2`.
6. `runMinutes` opseg `0-1440`.

Napomena:

1. Legacy polja (`giri`, `delay`, `giri1`, `giri2`, `gdelay`) su i dalje podrzana radi kompatibilnosti.
2. Dashboard sada salje nova TPD polja kao JSON.

## Podrazumevano ponasanje tajmera

1. `runMinutes = 0` znaci rad bez vremenskog limita.
2. `POST /api/start` pokrece sistem.
3. Ako je `runMinutes > 0`, sistem se sam zaustavlja po isteku vremena.
4. `POST /api/stop` odmah zaustavlja sve motore (fail-safe stop).

## Nova telemetrija u statusu

`/api/status`, `/api/stats` i `/api/health` sada vracaju dodatne metrike:

1. `rotationsToday`
2. `rotationsLastHour`
3. `currentTPD`
4. `driftPercent`
5. `currentTPDProgress`
6. `remainingRotations`
7. `motorState`
8. `errorCode`
9. `uptime`

## Pokretanje dashboard-a

Lokalno na Raspberry Pi:

```bash
cd pi-dashboard
python3 -m http.server 8080
```

Dashboard URL:

```text
http://localhost:8080
```

## Docker deploy za lokalnu mrezu

```bash
cd pi-dashboard
docker compose up -d --build
```

Otvaranje sa drugih uredjaja u istoj mrezi:

```text
http://IP_RASPBERRY_PI:8080
```

Gasenje:

```bash
docker compose down
```

Napomena:

1. Nemoj raditi port forwarding na ruteru za port `8080` ako zelis samo LAN pristup.
2. Dashboard podrazumevano koristi URL `http://192.168.1.111`, ali ga mozes promeniti u panelu.
