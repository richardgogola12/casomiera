# Časomiera pre hasičský šport (ESP32)

Časomiera pre ESP32 (Arduino IDE) s presnosťou 0,01 s. Režimy LP+PP útok, iba LP a odpočet 1–9 min. Verzia **1.2**.

📖 **Kompletná dokumentácia: [docs/DOKUMENTACIA.md](docs/DOKUMENTACIA.md)**

## Funkcie
- 5 vstupov cez optočlen, každý s vlastným prerušením (LP, PP, ŠTART fotobunka, CHANGE, RESET)
- meranie v prerušení z hardvérového časovača (1 µs), odosielanie beží na druhom jadre a meranie neovplyvní
- 2 relé pre LED terčov (T1 = LP = LED1, T2 = PP = LED2)
- LCD 20×4 cez I2C, časy vo formáte `min:sek:stotiny`, po štarte WiFi údaje
- výstup 10× za sekundu na USB, RS232, HC-12 a UDP (port 5000 mobil, 5001 LED displej) + okamžite pri udalosti
- binárny protokol kompatibilný s originálnou časomierou (rámec `RW:` 40 bajtov), udalosti `START` / `STOP`, príkazy `RW:RST` / `RW:CHNG`
- vlastná WiFi sieť `Casomiera` / heslo `hasici123`, IP `192.168.4.1`
- nastavenia na SD karte (`/casomiera.cfg`), bez SD v EEPROM

## Rýchly štart
1. Arduino IDE, doska **ESP32 Dev Module**, jadro esp32 3.x
2. Knižnica **LiquidCrystal I2C** (Frank de Brabander)
3. Otvor `Casomiera/Casomiera.ino` a nahraj
4. Voliteľne skopíruj [`docs/casomiera.cfg`](docs/casomiera.cfg) na SD kartu

## Štruktúra
```
Casomiera/Casomiera.ino   firmvér
docs/DOKUMENTACIA.md      zapojenie, ovládanie, protokol, nastavenia, riešenie problémov
docs/casomiera.cfg        vzorové nastavenia pre SD kartu
```
