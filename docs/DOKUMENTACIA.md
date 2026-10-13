# Časomiera pre hasičský šport – dokumentácia

Verzia firmvéru: **1.2** · Platforma: **ESP32** (Arduino IDE, jadro esp32 3.x)

## Obsah

1. [Prehľad](#1-prehľad)
2. [Hardvér a zapojenie](#2-hardvér-a-zapojenie)
3. [Nahratie firmvéru](#3-nahratie-firmvéru)
4. [Režimy merania](#4-režimy-merania)
5. [Ovládanie a menu](#5-ovládanie-a-menu)
6. [LCD displej](#6-lcd-displej)
7. [LED terčov a relé](#7-led-terčov-a-relé)
8. [Komunikácia a výstupy](#8-komunikácia-a-výstupy)
9. [Protokol](#9-protokol)
10. [WiFi a UDP](#10-wifi-a-udp)
11. [Nastavenia – SD karta a EEPROM](#11-nastavenia--sd-karta-a-eeprom)
12. [Presnosť merania](#12-presnosť-merania)
13. [Riešenie problémov](#13-riešenie-problémov)
14. [História verzií](#14-história-verzií)

---

## 1. Prehľad

Časomiera meria požiarny útok s presnosťou na **0,01 s**. Štart spúšťa fotobunka, cieľ zostrelenie terčov cez magnetické spínače.

| Vlastnosť | Hodnota |
|---|---|
| Rozlíšenie merania | 1 µs (hardvérový časovač `esp_timer`) |
| Zobrazenie | stotiny sekundy, orezané (nie zaokrúhlené) |
| Vstupy | 5, cez optočlen, každý s vlastným prerušením |
| Výstupy | 2 relé (LED terčov), LCD 20×4, USB, RS232, HC-12, UDP |
| Odosielanie dát | 10× za sekundu na všetky výstupy + okamžite pri udalosti |
| Režimy | LP+PP útok, iba LP, odpočet 1–9 min |
| Nastavenia | SD karta, bez SD EEPROM (interná pamäť ESP32) |

**Architektúra:** ESP32 má dve jadrá. Meranie (prerušenia), vyhodnotenie, relé, LCD a tlačidlá bežia na **jadre 1**. Všetko odosielanie (UDP, RS232, HC-12, USB) beží v samostatnej úlohe na **jadre 0** spolu s WiFi. Čas zostrelenia sa zapisuje priamo v prerušení, takže komunikácia, displej ani WiFi nemôžu ovplyvniť nameraný čas.

---

## 2. Hardvér a zapojenie

### 2.1 Zoznam súčiastok

| Súčiastka | Poznámka |
|---|---|
| ESP32 DevKit (ESP32-WROOM-32) | v Arduino IDE doska „ESP32 Dev Module“ |
| 5× rýchly optočlen | odporúčaný 6N137 (< 1 µs), PC817 je pomalší (10–20 µs) |
| 2× relé modul | 3,3 V ovládanie, spína 12 V pre LED terčov |
| LCD 20×4 s I2C prevodníkom (PCF8574) | adresa 0x27, niekedy 0x3F |
| MAX3232 modul | prevodník úrovní pre RS232, napájať z 3,3 V |
| HC-12 | rádiový modul 433 MHz |
| čítačka microSD (SPI) | voliteľná |
| fotobunka | štart; čo najkratšia doba odozvy (ideálne < 1 ms) |
| 2× magnetický (jazýčkový) spínač | terče LP a PP |
| 2× tlačidlo | CHANGE a RESET |

### 2.2 Piny

| Funkcia | GPIO | Poznámka |
|---|---|---|
| Vstup 1 – LP (ľavý prúd) | 34 | iba vstup, **externý pull-up 4k7 na 3V3 nutný** |
| Vstup 2 – PP (pravý prúd) | 35 | iba vstup, **externý pull-up 4k7 na 3V3 nutný** |
| Vstup 3 – ŠTART (fotobunka) | 32 | interný pull-up, odporúčaný aj externý 4k7 |
| Vstup 4 – CHANGE | 33 | interný pull-up |
| Vstup 5 – RESET | 27 | interný pull-up |
| Relé T1 – LP – LED1 | 25 | |
| Relé T2 – PP – LED2 | 26 | |
| LCD SDA | 21 | I2C 400 kHz |
| LCD SCL | 22 | |
| SD SCK | 18 | SPI |
| SD MISO | 19 | |
| SD MOSI | 23 | |
| SD CS | 5 | |
| RS232 RX (Serial2) | 16 | ← R1OUT na MAX3232 |
| RS232 TX (Serial2) | 17 | → T1IN na MAX3232 |
| HC-12 RX (Serial1) | 14 | ← TXD na HC-12 |
| HC-12 TX (Serial1) | 13 | → RXD na HC-12 |
| HC-12 SET | 4 | HIGH = normálny režim |
| USB (Serial) | – | vstavaný prevodník CP210x na doske |

### 2.3 Vstupy cez optočlen

Všetky vstupy sú **aktívne v LOW**: tranzistor optočlena stiahne vstup ESP32 na GND. Konštanta `INPUT_ACTIVE_LOW` v kóde to môže otočiť.

```
            3V3
             │
            [4k7]  pull-up
             │
 GPIO ───────┼──────── kolektor optočlena
                       emitor optočlena ──── GND (ESP32)

 Strana snímača:  +12/24 V ── [rezistor] ── LED optočlena ── spínač ── GND snímača
```

- Pri magnetických spínačoch platí **prvá hrana**. Zákmity po nej sa ignorujú, preto netreba hardvérové ošetrenie.
- Zostrelenie skôr ako `min_run_ms` (predvolene 1000 ms) po štarte sa ignoruje. Chráni to pred falošným signálom pri štarte.
- Tlačidlá CHANGE a RESET majú softvérové ošetrenie zákmitov (`debounce_ms`, predvolene 30 ms).

### 2.4 Relé

Väčšina modulov spína pri LOW (`relay_active_low=1`). Ak relé spína naopak, nastav `relay_active_low=0`. Po zapnutí sú obe relé vypnuté.

### 2.5 RS232 (MAX3232)

| ESP32 | MAX3232 modul | DB9 |
|---|---|---|
| GPIO17 (TX) | TXD / T1IN | pin 2 alebo 3 (podľa kábla) |
| GPIO16 (RX) | RXD / R1OUT | pin 3 alebo 2 |
| GND | GND | pin 5 |
| 3V3 | VCC | – |

Ak dáta neprichádzajú, najčastejšou príčinou je prekríženie TX/RX. Skús null-modem kábel alebo vymeň piny 2 a 3 na DB9.

---

## 3. Nahratie firmvéru

1. Nainštaluj **Arduino IDE 2.x**. V správcovi dosiek nainštaluj **esp32** od Espressif (verzia 3.x).
2. V správcovi knižníc nainštaluj **LiquidCrystal I2C** od Frank de Brabander. Varovanie, že knižnica je pre AVR, nevadí.
3. Otvor `Casomiera/Casomiera.ino`.
4. Nastavenia v menu **Tools**:

| Položka | Hodnota |
|---|---|
| Board | **ESP32 Dev Module** (nie ESP32-WROOM-DA, ten používa GPIO25 na prepínanie antén) |
| CPU Frequency | 240 MHz (WiFi/BT) |
| Flash Size | 4 MB |
| Partition Scheme | Default 4MB with spiffs |
| Upload Speed | 921600 (pri problémoch 115200) |
| Port | COM port s „Silicon Labs CP210x“ |

5. Klikni **Upload**. Ak sa zasekne na `Connecting...` s chybou „Wrong boot mode“, podrž tlačidlo **BOOT**, kým sa nezačne `Writing at 0x...`.

---

## 4. Režimy merania

| Režim | Názov | Štart | Koniec merania | Výsledok |
|---|---|---|---|---|
| **1** (predvolený) | LP+PP útok | fotobunka | padnú oba terče | horší (väčší) z časov LP a PP |
| **2** | iba LP | fotobunka | padne terč LP | čas LP |
| **3** | odpočet | fotobunka | uplynie nastavený čas (1–9 min) | 0:00:00 |

- **Automatické zastavenie:** Pri 999,99 s sa meranie zastaví samo.
- **Ručné zastavenie:** Tlačidlo RESET počas behu zastaví všetky ešte bežiace časy (pozri kapitolu 5).
- Režim sa mení v menu alebo na SD karte (`mode`).

---

## 5. Ovládanie a menu

### 5.1 Tlačidlá

| Tlačidlo | Stav | Akcia |
|---|---|---|
| RESET krátko | beží | zastaví časy, ktoré ešte bežia (nezostrelený terč dostane aktuálny čas, odpočet sa zastaví) |
| RESET krátko | stop / pripravené | vynuluje časomieru |
| CHANGE krátko | beží alebo stop, režim 1 | prepne zobrazený čas LP ↔ PP |
| CHANGE podržať 2 s | nebeží | otvorí menu |

Rovnaké akcie vyvolajú príkazy `RW:RST` a `RW:CHNG` cez USB, RS232 alebo HC-12 (kapitola 9.4).

### 5.2 Zobrazovaný čas

- V režime s jedným kanálom (režim 2 a odpočet) sa vždy zobrazuje kanál **0**.
- V režime 1 sa automaticky zobrazuje **väčší** z časov LP a PP. Počas behu je to ten, čo ešte beží, po zastavení horší výsledok.
- Tlačidlo CHANGE prepína medzi 0 (LP) a 1 (PP). Ručný výber sa na LCD označí `*`. Po ďalšom zastavení alebo resete sa výber vráti na automatický.
- Zobrazovaný kanál sa posiela v bajte 5 protokolu.

### 5.3 Menu

Menu sa otvorí podržaním CHANGE na 2 s, keď časomiera nebeží.

| Položka | RESET krátko mení |
|---|---|
| Rezim | 1 LP+PP → 2 LP → 3 ODPOCET |
| Odpocet | 1 → 2 → … → 9 min |
| LED1 LP | 0 RELE ↔ 1 EXTERNE |
| LED2 PP | 0 RELE ↔ 1 EXTERNE |
| ULOZIT A ODIST | uloží a zatvorí menu |

- **CHANGE krátko** posunie na ďalšiu položku.
- **CHANGE podržať 2 s** uloží a zatvorí menu.
- Pri položke LED sa relé daného terča na skúšku zopne, ak je nastavené na 0 (RELE).
- Uloženie ide na SD kartu, a ak nie je vložená, do EEPROM.

---

## 6. LCD displej

Časy sa zobrazujú vo formáte **min:sek:stotiny**, napr. `0:12:87` alebo `5:00:98`.

**Úvodná obrazovka** sa zobrazí 8 s po zapnutí. Skončí skôr stlačením tlačidla alebo štartom.
```
WiFi: Casomiera
Heslo: hasici123
IP: 192.168.4.1
UDP port 5000 / 5001
```

**Režim 1 počas behu:**
```
1:LP+PP         BEZI
PP:          0:12:87
LP0:12:41 PP0:12:87
W:AP SD REL:12
```
- Riadok 1: režim a stav (PRIPRAVENE / BEZI / STOP).
- Riadok 2: zobrazovaný čas, `*` pri ručnom výbere.
- Riadok 3: časy oboch prúdov. Nezastavený prúd ukazuje bežiaci čas.
- Riadok 4: WiFi (`AP` vlastná sieť, `ST` klient, `--` nefunguje), odkiaľ sú nastavenia (`SD` alebo `EE` = EEPROM) a ktoré relé sa používajú (`1`, `2`, `-`).

**Režim 3 (odpočet):**
```
3:ODPOCET       BEZI
ODPOCET:     3:59:87
Odpocet z 4 min
W:AP SD REL:12
```

---

## 7. LED terčov a relé

| Výstup | Prúd | Svetlo | Nastavenie |
|---|---|---|---|
| T1 (GPIO25) | LP – ľavý | LED1 | `led_t1` |
| T2 (GPIO26) | PP – pravý | LED2 | `led_t2` |

| Hodnota `led_t1` / `led_t2` | Správanie |
|---|---|
| **0** (predvolené) | relé zopne, keď sa daný čas zastaví |
| **1** | svetlo napája niečo iné, relé sa nepoužíva |

- Relé zopne pri zostrelení terča aj pri ručnom zastavení tlačidlom RESET.
- V režime 2 sa používa iba T1.
- V odpočte zopnú relé (nastavené na 0) na konci odpočtu.
- Relé sa vypnú pri resete.

---

## 8. Komunikácia a výstupy

| Výstup | Rozhranie | Predvolené nastavenie | Obsah |
|---|---|---|---|
| USB | vstavaný CP210x (napr. COM6) | 115200 baud, 8N1 | rámec `RW:` + udalosti |
| RS232 | Serial2 cez MAX3232 | 9600 baud, 8N1 | rámec `RW:` + udalosti |
| HC-12 | Serial1 | 9600 baud, 8N1 | rámec `RW:` + udalosti |
| UDP mobil | WiFi | port 5000 | rámec `RW:` (alebo text pri `udp_text=1`) + udalosti |
| UDP LED displej | WiFi | port 5001 | rámec `RW:` (alebo text) + udalosti |

**Časovanie:**
- Rámec sa posiela **vždy**: v stave pripravené, počas behu, po zastavení aj v odpočte.
- Pravidelne každých **100 ms**, teda 10× za sekundu (`net_interval_ms`, `ser_interval_ms`).
- **Okamžite** (do ~5 ms) pri štarte, zostrelení terča, zastavení, resete a prepnutí zobrazenia.

**USB:** Na USB idú v predvolenom stave iba binárne rámce, preto ich Serial Monitor ukazuje ako nečitateľné znaky. Ladiace texty (`[START]`, `[STOP] 12.87 s` atď.) zapne `usb_debug=1`, vtedy sa ale na USB neposielajú rámce.

**PC program na USB:** Ak sa ESP32 pri otvorení portu reštartuje, vypni v nastaveniach portu DTR a RTS.

---

## 9. Protokol

Protokol je zhodný s originálnou časomierou, ako ho číta PC program (`ufrmMain.pas`, `ComPort1RxChar`).

### 9.1 Rámec `RW:` (40 bajtov)

| Bajt | Obsah | Hodnoty |
|---|---|---|
| 0–2 | hlavička | `52 57 3A` = `RW:` |
| 3 | režim | `01` stopky (režim 1 a 2), `02` odpočet |
| 4 | počet kanálov | `02` LP+PP, `01` LP alebo odpočet |
| 5 | zobrazovaný čas | `00` LP, `01` PP (pozri 5.2) |
| 6 | stav | `01` resetnutá / pripravená, `00` inak |
| 7–9 | T1 (LP) čas | ms, 3 bajty little-endian (najnižší prvý) |
| 10 | T1 stav | pozri 9.2 |
| 11–13 | T2 (PP) čas | ms, little-endian |
| 14 | T2 stav | |
| 15–18 | T3 | nepoužité, `00` |
| 19–22 | T4 | nepoužité, `00` |
| 23–38 | rezerva | `00` |
| 39 | koniec | `0D` (CR) |

Čas v ms = `bajt[7] + bajt[8]·256 + bajt[9]·65536`. Časy sú vždy násobky 10 ms.

### 9.2 Stav kanála

| Hodnota | Význam |
|---|---|
| `00` | nulový / nepoužitý (čas 0) |
| `81` | beží |
| `82` | zastavený senzorom (zostrelený terč) |
| `84` | ukončený: ručne tlačidlom RESET, koniec odpočtu alebo limit 999,99 s |

### 9.3 Príklady

Pripravené, režim 1:
```
52 57 3A 01 02 00 01  00 00 00 00  00 00 00 00  00 ... 00  0D
```

Beží 5,00 s, režim 1 (5000 ms = 0x001388):
```
52 57 3A 01 02 00 00  88 13 00 81  88 13 00 81  00 ... 00  0D
```

Stop, LP 12,41 s (0x307A), PP 12,87 s (0x3246), zobrazený PP (väčší):
```
52 57 3A 01 02 01 00  7A 30 00 82  46 32 00 82  00 ... 00  0D
```

Odpočet pripravený, 4 min (240000 ms = 0x03A980):
```
52 57 3A 02 01 00 01  80 A9 03 00  00 00 00 00  00 ... 00  0D
```

### 9.4 Udalosti a príkazy

**Udalosti** (časomiera → prijímač) idú raz, hneď za rámcom s novým stavom, na všetky výstupy. Sú ukončené iba CR.

| Text | Kedy |
|---|---|
| `START` | štart fotobunkou (stopky aj odpočet) |
| `STOP` | zastavili všetky potrebné časy: LP+PP / LP / koniec odpočtu / ručne tlačidlom RESET |

PC program riadky, ktoré nezačínajú `RW:`, ignoruje.

**Príkazy** (prijímač → časomiera) sa prijímajú cez USB, RS232 aj HC-12 a sú ukončené CR alebo LF.

| Príkaz | Akcia |
|---|---|
| `RW:RST` | ako tlačidlo RESET |
| `RW:CHNG` | ako tlačidlo CHANGE (krátko) |

### 9.5 Udalosti v PC programe

| Udalosť PC programu | Podmienka v rámci |
|---|---|
| OnStart | bajt 6 = `00` a kanály `81` |
| OnStopAll | všetky použité kanály `82`, alebo všetky `84` |
| OnReset | bajt 6 = `01` a kanály `00` s časom 0 |

Pri ručnom zastavení, keď jeden terč padol (`82`) a druhý zastavil RESET (`84`), sa OnStopAll nespustí. Je to zámer PC programu: reaguje iba na normálne zastavenie senzorom.

### 9.6 Bajt 0x0D v dátach

PC program končí rámec na prvom bajte `0D`. Najnižší bajt času je vždy párny (násobky 10 ms), takže `0D` môže vzniknúť iba v strednom bajte, napr. pri čase 3,33–3,58 s alebo 68,86–69,12 s. Počas behu sa taký rámec vynechá a displej na ~0,25 s podrží predchádzajúci čas. Rámce s udalosťou a po zastavení sa posielajú vždy.

### 9.7 Textový formát (voliteľný, iba UDP)

Pri `udp_text=1` ide na UDP namiesto binárneho rámca riadok:
```
$FT,<režim>,<stav>,<hlavný čas>,<LP>,<PP>,<zobrazenie>*<XOR>\r\n
```
- stav: `R` pripravené, `B` beží, `S` stop, `M` menu
- LP / PP: čas zostrelenia alebo `-`
- XOR: kontrolný súčet znakov medzi `$` a `*` (hex), ako pri NMEA

Príklad: `$FT,1,S,12.87,12.41,12.87,1*69`

---

## 10. WiFi a UDP

| Parameter | Predvolená hodnota |
|---|---|
| Režim | AP – časomiera vytvorí vlastnú sieť |
| SSID | `Casomiera` |
| Heslo | `hasici123` |
| IP časomiery | `192.168.4.1` |
| Max. zariadení | 8 |
| Port mobil | 5000 |
| Port LED displej | 5001 |

**Pripojenie:**
- **Mobil:** pripoj sa na sieť `Casomiera` a v aplikácii (napr. „UDP Monitor“) počúvaj na porte 5000.
- **ESP32 s LED displejom:** pripojí sa ako klient do siete `Casomiera` a počúva na porte 5001.

**Doručovanie:** Časomiera si každú sekundu zistí IP adresy pripojených zariadení a posiela dáta každému priamo (unicast). Je to spoľahlivejšie ako broadcast, ktorý mobily v úspornom režime zahadzujú. Ak je v nastaveniach zadaná pevná IP (`phone_ip`, `disp_ip`), dáta idú iba na ňu.

**Klientsky režim:** `wifi_mode=STA` pripojí časomieru k existujúcej sieti (`ssid`, `pass`). Ak sa pripojenie do 10 s nepodarí, časomiera spustí vlastnú sieť s predvolenými údajmi.

---

## 11. Nastavenia – SD karta a EEPROM

### 11.1 Poradie načítania

1. **SD karta** – súbor `/casomiera.cfg`. Ak je karta vložená a súbor sa dá prečítať, má prednosť a EEPROM sa nepoužíva.
2. **EEPROM** (interná pamäť ESP32, NVS) – ak SD karta nie je alebo sa z nej nedá čítať.
3. **Predvolené hodnoty v programe** (konštanty `DEF_...`) – ak nie je ani jedno. Hneď sa uložia do EEPROM.

- Ak je SD karta vložená, ale súbor na nej chýba, vytvorí sa z aktuálnych nastavení.
- Uloženie z menu ide na SD kartu, bez nej do EEPROM.
- Na LCD v poslednom riadku vidíš zdroj nastavení: `SD` alebo `EE`.

### 11.2 Formát súboru

Textový súbor, jeden riadok `kluc=hodnota`. Riadky začínajúce `#` sú komentáre. Neznáme kľúče sa ignorujú. Vzor je v [`casomiera.cfg`](casomiera.cfg).

### 11.3 Kľúče

| Kľúč | Predvolené | Rozsah / hodnoty | Popis |
|---|---|---|---|
| `mode` | 1 | 1–3 | 1 LP+PP, 2 LP, 3 odpočet |
| `countdown_min` | 4 | 1–9 | dĺžka odpočtu v minútach |
| `led_t1` | 0 | 0 / 1 | LED1 (LP): 0 = relé zopne, 1 = napája niečo iné |
| `led_t2` | 0 | 0 / 1 | LED2 (PP) |
| `relay_active_low` | 1 | 0 / 1 | 1 = relé spína pri LOW |
| `min_run_ms` | 1000 | ms | zostrelenie skôr po štarte sa ignoruje |
| `debounce_ms` | 30 | 5–500 | ošetrenie zákmitov tlačidiel |
| `net_interval_ms` | 100 | 20–100 | interval odosielania UDP |
| `ser_interval_ms` | 100 | 20–100 | interval odosielania USB / RS232 / HC-12 |
| `lcd_interval_ms` | 100 | 50–500 | obnova LCD |
| `wifi_mode` | AP | AP / STA | vlastná sieť / pripojenie k sieti |
| `ssid` | Casomiera | text | názov siete |
| `pass` | hasici123 | min. 8 znakov | heslo siete |
| `phone_ip` | broadcast | broadcast / IP | cieľ UDP pre mobil |
| `phone_port` | 5000 | port | |
| `disp_ip` | broadcast | broadcast / IP | cieľ UDP pre LED displej |
| `disp_port` | 5001 | port | |
| `rs232_baud` | 9600 | baud | |
| `hc12_baud` | 9600 | baud | musí sedieť s nastavením HC-12 |
| `usb_baud` | 115200 | baud | |
| `lcd_addr` | 0x27 | 0x27 / 0x3F | I2C adresa LCD |
| `use_udp` | 1 | 0 / 1 | zapnúť UDP |
| `use_rs232` | 1 | 0 / 1 | zapnúť RS232 |
| `use_hc12` | 1 | 0 / 1 | zapnúť HC-12 |
| `use_usb` | 1 | 0 / 1 | rámce na USB |
| `usb_debug` | 0 | 0 / 1 | 1 = ladiace texty na USB namiesto rámcov |
| `udp_text` | 0 | 0 / 1 | 1 = UDP posiela text `$FT` namiesto rámca |

Hodnoty `1`, `true`, `ano`, `zap`, `on` znamenajú zapnuté.

---

## 12. Presnosť merania

| Zdroj chyby | Typická hodnota |
|---|---|
| Rozlíšenie časovača | 1 µs |
| Oneskorenie prerušenia | 2–10 µs, zriedka do ~50 µs |
| Kryštál 40 MHz (±10–20 ppm) | 0,2–0,4 ms pri 20 s |
| Rýchly optočlen 6N137 | < 1 µs |
| Optočlen PC817 | 10–20 µs |
| WiFi, LCD, odosielanie | 0 (bežia mimo merania) |

- Elektronika časomiery spolu prispieva chybou približne **0,5 ms**, čo je 20× menej ako jedna stotina.
- Najväčšou neistotou je **doba odozvy fotobunky** (1–10 ms podľa typu). Ak majú štart a cieľ podobné oneskorenie, čiastočne sa vyrušia.
- Časy sa **orezávajú** na stotiny: 12,879 s sa zobrazí ako 12,87 s.
- Zápis do EEPROM pozdrží prerušenia na pár ms, ukladá sa ale iba v menu, keď časomiera nebeží.

**Overenie:** Na štart aj cieľ priveď impulz zo zdroja so známym odstupom, napr. presne 10,000 s. Časomiera má ukázať 10,00 s, prípadne 9,99 s pri orezaní hraničnej hodnoty.

---

## 13. Riešenie problémov

| Problém | Riešenie |
|---|---|
| Kompilácia: `'Snap' does not name a type` | starší kód; aktuálna verzia má štruktúry na začiatku súboru |
| Nahrávanie: `Wrong boot mode detected (0x13)` | podrž BOOT počas `Connecting...`; trvalo: kondenzátor 10 µF medzi EN a GND |
| Nahrávanie: port `[not connected]` | Tools → Port → COM s „CP210x“ |
| LCD nič neukazuje | skontroluj `lcd_addr` (0x27 / 0x3F) a kontrast; pri nezmysloch zmeň `I2C_CLOCK` na 100000 |
| RS232 nič neprijíma | prekrížené TX/RX (null-modem alebo piny 2↔3), napájanie MAX3232 z 3,3 V, zhodný baud |
| PC program zahadzuje rámce | port musí byť nastavený na rovnaký baud; na USB vypni `usb_debug` |
| ESP32 sa reštartuje pri otvorení portu | v PC programe vypni DTR a RTS |
| Mobil nedostáva UDP | pripojený na sieť `Casomiera`, aplikácia počúva na porte 5000, mobilné dáta vypnúť |
| Vstupy LP / PP reagujú náhodne | chýba externý pull-up 4k7 na GPIO34 a 35 |
| Relé spína naopak | zmeň `relay_active_low` |
| Relé nezopne | `led_t1` / `led_t2` musí byť 0 |
| Terč sa nezapočíta | zostrelenie skôr ako `min_run_ms` po štarte sa ignoruje |

Pre ladenie nastav `usb_debug=1` a otvor Serial Monitor na 115200. Uvidíš udalosti `[START]`, `[LP] 12.41 s`, `[STOP] 12.87 s`, `[RESET]`, `[CMD] RST` a odkiaľ sa načítali nastavenia.

---

## 14. História verzií

| Verzia | Zmeny |
|---|---|
| 1.2 | udalosti `START` a `STOP` na všetky výstupy |
| 1.1 | zobrazovaný čas (väčší, CHANGE prepína 0↔1), LED/relé podľa `led_t1`/`led_t2`, odpočet 1–9 min, LCD formát `m:ss:cc`, WiFi údaje pri štarte, EEPROM ako záloha SD |
| 1.0 | základná časomiera, protokol `RW:` zhodný s originálom (40 B), USB / RS232 / HC-12 / UDP 10× za sekundu, odosielanie na jadre 0, vlastná WiFi, príkazy `RW:RST` / `RW:CHNG`, RESET počas behu zastaví bežiace časy |
