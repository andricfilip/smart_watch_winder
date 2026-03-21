# Watch Winder Modern Dashboard

Novi frontend (React + Vite) koji radi sa istim ESP8266 API endpointima kao postojeci dashboard.

## Korisnicko uputstvo

- Detaljno uputstvo za upotrebu: [USER_GUIDE_SR.md](USER_GUIDE_SR.md)
- Detailed user guide in English: [USER_GUIDE_EN.md](USER_GUIDE_EN.md)

## API

- GET /api/status
- GET /api/stats
- GET /api/health
- POST /api/config
- POST /api/start
- POST /api/stop

## Lokalni razvoj

1. `cd pi-dashboard-modern`
2. `npm install`
3. `npm run dev`
4. Otvori `http://localhost:5173`

## Docker

1. `cd pi-dashboard-modern`
2. `docker compose up --build -d`
3. Otvori `http://localhost:8090`

## Startup konfiguracija (bez menjanja UI)

U `docker-compose.yml`:

- `APP_DEVICE_MODE`: `single` ili `double`
- `APP_LOCK_DEVICE_MODE`: `true/false`
- `APP_DEFAULT_DEVICE_URL`: npr. `http://192.168.1.111`
- `APP_LOCK_DEVICE_URL`: `true/false`
- `APP_LANGUAGE`: `sr` ili `en`
- `APP_LOCK_LANGUAGE`: `true/false`

Ako su lock opcije `true`, frontend skriva odgovarajuca polja i koristi vrednosti iz konfiguracije.
