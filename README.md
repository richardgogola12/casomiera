# Časomiera pre hasičský šport (ESP32)

Časomiera pre ESP32 (Arduino IDE) s presnosťou 0,01 s. Režimy LP+PP útok, iba LP a odpočet 1–9 min.

## Funkcie
- 5 vstupov cez optočlen, každý s vlastným prerušením (LP, PP, ŠTART fotobunka, CHANGE, RESET)
- 2 relé pre LED terčov (T1 = LP = LED1, T2 = PP = LED2)
- LCD 20x4 cez I2C, časy vo formáte `min:sek:stotiny`
- Výstup 10× za sekundu na USB, RS232, HC-12 a UDP (port 5000 mobil, 5001 LED displej)
- Binárny protokol kompatibilný s originálnou časomierou (rámec `RW:` 40 bajtov, príkazy `RW:RST`, `RW:CHNG`)
- Vlastná WiFi sieť `Casomiera` / heslo `hasici123`, IP `192.168.4.1`
- Nastavenia na SD karte (`/casomiera.cfg`), bez SD v EEPROM (interná pamäť ESP32)

## Nahratie
1. Arduino IDE, doska **ESP32 Dev Module**, jadro esp32 3.x
2. Knižnica **LiquidCrystal I2C** (Frank de Brabander)
3. Otvor `Casomiera/Casomiera.ino` a nahraj

Zapojenie pinov, protokol a ovládanie sú popísané v hlavičke `Casomiera.ino`.
