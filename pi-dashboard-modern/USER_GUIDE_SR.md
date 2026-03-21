# Uputstvo za koriscenje aplikacije (Modern Dashboard)

Ovo je prakticno uputstvo: sta da podesis, koje vrednosti da uneses i kakav rezultat dobijas na watch winder uredjaju.

## 1. Sta aplikacija radi

Aplikacija upravlja ESP8266 firmware-om preko lokalne mreze.

Podrzano:

1. pokretanje i zaustavljanje navijanja
2. podesavanje TPD logike
3. izbor smera i rezima rada
4. pracenje metrike rada i zdravlja sistema

## 2. Brzi start (3 minuta)

1. Uveri se da je firmware uploadovan na ESP:
   - single: `winder/winder.ino`
   - double: `doubleWinder/doubleWinder.ino`
2. Pokreni dashboard:
   - `cd pi-dashboard-modern`
   - `docker compose up --build -d`
3. Otvori u browser-u: `http://IP_RPI:8090` (ili `http://localhost:8090` na samom Pi)
4. Klikni `Osvezi`.
5. Podesi parametre i klikni `Sacuvaj podesavanja`.
6. Klikni `Start`.

Ako je status `Van mreze`, proveri da su ESP i dashboard u istoj mrezi i da je URL tacan.

## 3. Parametri i sta dobijas

## targetTPD (500-1000)

Znacenje:

- broj rotacija koje zelis u okviru 24h

Efekat:

- veci TPD = cesce aktiviranje i vise ukupnih rotacija
- manji TPD = redje aktiviranje i mirniji rad

Preporuka:

- kreni sa `650`, pa fino podesi po ponasanju sata

## direction (CW / CCW / BIDIR)

Znacenje:

- smer navijanja

Efekat:

- `CW`: samo u smeru kazaljke
- `CCW`: samo suprotno
- `BIDIR`: naizmenicno, bezbedan izbor ako nisi siguran koji smer sat trazi

## mode (STANDARD / SMART)

Znacenje:

- nacin raspodele ciklusa

Efekat:

- `STANDARD`: regularna raspodela kroz dan
- `SMART`: inteligentnije menjanje i balansiranje ciklusa

## speed (1-5)

Znacenje:

- brzina step motora

Efekat:

- `1-2`: tisi i mirniji rad
- `4-5`: brze izvrsenje, potencijalno vise buke

Preporuka:

- koristi `2` ili `3` za stabilan i tih rad

## activeHours

Znacenje:

- satnica kada je dozvoljen aktivan rad

Efekat:

- van tog intervala sistem pauzira

Primer:

- `8-23`: radi tokom dana/veceri, miruje nocu

## smartSwitchRotations

Znacenje:

- na koliko rotacija menja smer u SMART rezimu

Efekat:

- veca vrednost = redje menjanje smera
- manja vrednost = cesce menjanje smera

## quietMode + quietStartHour + quietEndHour + nightSpeedCap

Znacenje:

- nocni tisi profil

Efekat:

- u quiet periodu brzina se limitira na `nightSpeedCap`
- manje buke nocu

Primer:

- quiet od `22` do `7`, cap `2`

## runMinutes

Znacenje:

- koliko dugo da sistem radi pre auto-stop

Efekat:

- `0` = bez limita
- `>0` = automatski stop nakon zadatog vremena

## 4. Status panel (sta znaci svaka metrika)

1. `State`: trenutno stanje (`IDLE`, `RUNNING`, `PAUSED`, `ERROR`, `TIMER_DONE`)
2. `Error`: kod greske (ako postoji)
3. `Timer`: preostalo vreme timera
4. `Network`: mrezni mod i IP
5. `Rot today`: ukupne rotacije danas
6. `Rot hour`: rotacije u poslednjem satu
7. `Current TPD`: projekcija trenutnog TPD tempa
8. `Drift %`: odstupanje od planiranog dnevnog tempa
9. `Progress`: procenat ispunjenog dnevnog plana
10. `Remaining`: preostale rotacije do cilja
11. `Heap`: slobodna memorija na ESP-u
12. `Uptime`: vreme rada uredjaja

## 5. Preporucene pocetne vrednosti

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
5. active hours obe glave: `0-23`
6. quietMode: `true`

## 6. Single vs Double

1. Ako je firmware single, koristi single podešavanja.
2. Ako je firmware double, koristi double podešavanja.
3. Ne treba menjati mod u aplikaciji ako je lock ukljucen kroz Docker env.

## 7. Ceste situacije i resenja

## Van mreze

1. Proveri `APP_DEFAULT_DEVICE_URL` u `docker-compose.yml`.
2. Proveri da ESP vraca `GET /api/status` iz browser-a.
3. Proveri da su uredjaji u istoj LAN mrezi.

## Motor radi preglasno

1. spusti `speed` na `2`
2. ukljuci `quietMode`
3. smanji `nightSpeedCap` na `1` ili `2`

## Sat deluje da je premalo navijan

1. povecaj `targetTPD` u koracima po 50
2. proveri `Drift %` i `Remaining`

## 8. Docker konfiguracija (najvaznije)

U `pi-dashboard-modern/docker-compose.yml` koristis:

1. `APP_DEVICE_MODE`: `single` ili `double`
2. `APP_LOCK_DEVICE_MODE`: zakljucava mod u UI
3. `APP_DEFAULT_DEVICE_URL`: IP ESP uredjaja
4. `APP_LOCK_DEVICE_URL`: zakljucava URL polje
5. `APP_LANGUAGE`: `sr` ili `en`
6. `APP_LOCK_LANGUAGE`: zakljucava izbor jezika

Posle izmene env vrednosti uradi:

1. `docker compose up --build -d`
