# Watch Winder Uputstvo za Sklapanje

Elegant watch winder sa drvenim slotovima (3D printable paneli).
Ovo je novi, kompaktniji model sa opcijom Pogo pin kontakata za drugi winder.

## 1. Kratak pregled

Sistem koristi:

1. D1 Mini (ESP8266)
2. 28BYJ-48 stepper + ULN2003 driver
3. lezajeve i 3D print delove
4. opciono Pogo pinove za modularni drugi motor

Ako motor ne radi pravilno (vibrira, preskace, menja smer cudno), redosled 4 signalna voda izmedju D1 Mini i ULN2003 moze da varira u zavisnosti od motora. Menjaj redosled dok ne dobijes stabilnu rotaciju.

## 2. Materijal (BOM)

1. Pogo pins (4P + 2P par)
   https://s.click.aliexpress.com/e/_c2xJoAxh
2. D1 Mini microcontroller
   https://s.click.aliexpress.com/e/_c2yz2bsB
3. Stepper motor + ULN2003 driver (za svaki winder po 1 set)
   https://s.click.aliexpress.com/e/_c3bp08OR
4. 6001ZZ bearings
   https://s.click.aliexpress.com/e/_c3FSeHun

Napomena:

- Za single varijantu treba 1x motor+driver.
- Za double varijantu trebaju 2x motor+driver.
- Pogo pinovi su opcioni, ali preporuceni za modularni drugi winder.

## 3. Vizuelno povezivanje (single i double)

### Single winder wiring

![Single winder wiring](images/single-wiring.png)

### Double winder wiring

![Double winder wiring](images/double-wiring.png)

## 4. Pin mapping (firmware)

### Single firmware

Fajl: `winder/winder.ino`

- `IN1 = D5`
- `IN2 = D6`
- `IN3 = D7`
- `IN4 = D8`

### Double firmware

Fajl: `doubleWinder/doubleWinder.ino`

Motor A:

- `IN1_A = D5`
- `IN2_A = D6`
- `IN3_A = D7`
- `IN4_A = D8`

Motor B:

- `IN1_B = D4`
- `IN2_B = D3`
- `IN3_B = D2`
- `IN4_B = D1`

## 5. Korak-po-korak sklapanje

1. Sastavi mehanicki deo i ubaci 6001ZZ lezajeve.
2. Montiraj D1 Mini i ULN2003 driver(e) na nosace.
3. Povezi napajanje i GND zajednicki za sve module.
4. Povezi signalne vodove sa D1 Mini na ULN2003 prema pin mapiranju.
5. Povezi stepper konektor na ULN2003.
6. Ako koristis double varijantu, povezi i drugi driver.
7. Ako koristis Pogo pinove, proveri kontakt 4P (signal) i 2P (napajanje/GND).
8. Pre prvog zatvaranja kucista testiraj rotaciju sa malom brzinom.

## 6. Firmware izbor i upload

1. Za 1 sat upload `winder/winder.ino`.
2. Za 2 sata upload `doubleWinder/doubleWinder.ino`.

Pre upload-a podesi mrezu u firmware-u:

- `STA_SSID`
- `STA_PASSWORD`
- `ENABLE_AP_FALLBACK`
- `AP_SSID`
- `AP_PASSWORD`
- `STA_CONNECT_TIMEOUT_MS`

Preporuka:

1. Drzi `ENABLE_AP_FALLBACK = true` dok testiras.
2. Koristi jaku `AP_PASSWORD` lozinku (12+ karaktera).
3. `STA_CONNECT_TIMEOUT_MS` stavi 10000-20000.

## 7. Prvo pokretanje i test

1. Uploadaj firmware.
2. Otvori Serial Monitor na 115200 i procitaj IP adresu.
3. Pokreni dashboard iz foldera `pi-dashboard`.
4. U dashboard unesi URL uredjaja i klikni `Sacuvaj uredjaj`.
5. Klikni `Osvezi status`.
6. Podesi parametre i `Start`.
7. Proveri smer i stabilnost okretanja.

## 8. Dashboard i tajmer

- `runMinutes = 0`: bez limita rada.
- `runMinutes > 0`: automatski stop po isteku vremena.
- `Stop` je fail-safe i odmah otpusta motor.

## 9. Recommended starting setup

Single:

1. `giri = 3`
2. `speed = 2`
3. `delay = 7`

Double:

1. `giri1 = 3`, `speed1 = 2`
2. `giri2 = 3`, `speed2 = 2`
3. `gdelay = 7`

Zatim fino podesi u odnosu na sat i mehaniku nosaca.

## 10. Troubleshooting

1. Motor ne krece:
   - proveri napajanje i GND
   - proveri konektor motora na ULN2003
2. Motor vibrira bez rotacije:
   - promeni redosled 4 signalna voda
3. Dashboard je offline:
   - proveri IP i da su ESP i dashboard u istoj mrezi
4. Double ne radi na drugom motoru:
   - proveri pin mapiranje za Motor B
   - proveri 2P/4P Pogo kontakte

## 11. Video reference

Dodaj linkove ka svojim objavama:

- Part 1: <INSERT_LINK>
- Part 2: <INSERT_LINK>
- Final version: <INSERT_LINK>
