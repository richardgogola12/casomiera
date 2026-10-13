/******************************************************************************
 *  ČASOMIERA PRE HASIČSKÝ ŠPORT  –  ESP32 (Arduino IDE)
 *  verzia 1.0
 *
 *  VSTUPY (všetky cez rýchly optočlen, aktívne LOW, každý má vlastné prerušenie):
 *    1  LP      – zostrelenie ľavého terča (magnetický spínač)
 *    2  PP      – zostrelenie pravého terča (magnetický spínač)
 *    3  START   – štart časomiery (fotobunka)
 *    4  CHANGE  – zmena zobrazeného času po zastavení / ovládanie menu
 *    5  RESET   – reset / ovládanie menu
 *
 *  VÝSTUPY:
 *    T1 relé – LP – LED1 (12 V svetlo ľavého terča)
 *    T2 relé – PP – LED2 (12 V svetlo pravého terča)
 *      led_t1 / led_t2 = 1 -> svetlo napája niečo iné, relé sa nepoužíva
 *      led_t1 / led_t2 = 0 -> relé zopne, keď sa daný čas zastaví
 *    LCD 20x4 cez I2C (PCF8574)
 *    RS232 (Serial2 cez MAX3232)
 *    HC-12  (Serial1)
 *    UDP – telefón (port 5000) + druhé ESP32 s veľkým LED displejom (port 5001)
 *
 *  WIFI:
 *    Časomiera vytvorí vlastnú sieť  SSID "Casomiera", heslo "hasici123", IP 192.168.4.1
 *    Mobil aj ESP32 s LED displejom sa pripoja do tejto siete (max. 8 zariadení).
 *    Správa sa posiela priamo každému pripojenému zariadeniu na port 5000 aj 5001.
 *    UDP, RS232 aj HC-12: pevne 10x za sekundu (každých 100 ms) + okamžite pri udalosti.
 *    Všetko odosielanie beží v samostatnej úlohe na jadre 0, meranie (prerušenia)
 *    a hlavná slučka na jadre 1 -> odosielanie meranie neovplyvní.
 *
 *  REŽIMY:
 *    1 – LP + PP útok  (čas sa zastaví, keď padnú oba terče, výsledok = horší čas)
 *    2 – iba LP        (čas sa zastaví po zostrelení LP)
 *    3 – odpočet 1–9 min (nastaviteľné v menu, predvolene 4 min, štart fotobunkou)
 *
 *  OVLÁDANIE TLAČIDLAMI:
 *    RESET  krátko počas behu – zastaví časy, ktoré ešte bežia (nezostrelený terč
 *                               dostane aktuálny čas, jeho LED nesvieti; odpočet sa zastaví)
 *    RESET  krátko (nebeží)   – reset na "PRIPRAVENE"
 *    CHANGE krátko            – prepína zobrazený čas LP <-> PP (režim 1, počas behu aj po
 *                               stope). Bez stlačenia sa vždy ukazuje väčší čas.
 *    CHANGE podržať 2 s       – vstup do MENU (keď časomiera nebeží)
 *    MENU:    Režim / Odpočet 1–9 min / LED1 LP / LED2 PP / Uložiť
 *    V MENU:  CHANGE krátko = ďalšia položka
 *             RESET  krátko = zmena hodnoty / potvrdenie "ULOZIT A ODIST"
 *             CHANGE podržať 2 s = uložiť a odísť
 *
 *  LCD: časy vo formáte min:sek:stotiny (5:00:98). Po zapnutí 8 s ukazuje WiFi, heslo a IP
 *       (skončí stlačením tlačidla alebo štartom).
 *  NASTAVENIA: SD karta (/casomiera.cfg) má prednosť. Ak SD nie je alebo sa nedá prečítať,
 *       použije sa EEPROM (interná pamäť ESP32), inak predvolené hodnoty v programe.
 *       Uloženie z menu ide na SD, bez SD do EEPROM.
 *
 *  PRESNOSŤ:
 *    Čas sa odčítava v prerušení z hardvérového časovača esp_timer (rozlíšenie 1 µs).
 *    Zobrazuje sa na 0.01 s (orezaním, nie zaokrúhlením). Odosielanie, LCD a WiFi
 *    nemajú na zmeraný čas žiadny vplyv.
 *
 *  KNIŽNICE (Arduino IDE -> Správca knižníc):
 *    - "LiquidCrystal I2C" od Frank de Brabander
 *    Ostatné (WiFi, WiFiUdp, Wire, SPI, SD) sú súčasťou ESP32 jadra.
 *    Doska: "ESP32 Dev Module"
 *
 *  PROTOKOL – zhodný s originálnou časomierou (podľa ufrmMain.pas, ComPort1)
 *  Posiela sa VŽDY (resetnutá, beh, stop, odpočet) 10x za sekundu na USB (COM port ESP32),
 *  RS232, HC-12 aj UDP. Binárny rámec 40 bajtov:
 *    [0..2]  'R' 'W' ':'         (0x52 0x57 0x3A)
 *    [3]     režim               1 = stopky (režim 1 a 2), 2 = odpočet (režim 3)
 *    [4]     počet kanálov       2 = LP+PP, 1 = LP / odpočet
 *    [5]     zobrazovaný čas     1 kanál: vždy 0. 2 kanály: 0 = T1/LP, 1 = T2/PP –
 *                                automaticky ten väčší, tlačidlo CHANGE prepína 0 <-> 1
 *    [6]     stav                0x01 = resetnutá (pripravená), 0x00 = inak
 *    [7..10]  T1 (LP)  [11..14] T2 (PP)  [15..18] T3  [19..22] T4
 *             každý: čas v ms, 3 bajty little-endian (najnižší prvý) + stav:
 *             0x81 = beží, 0x82 = zastavený senzorom,
 *             0x84 = ukončený (ručne RESETOM / koniec odpočtu),
 *             0x00 = nulový / nepoužitý (čas 0)
 *    [23..38] 0x00 (rezerva, doplnenie na 40 bajtov)
 *    [39]    0x0D (CR)
 *    Príklad LP 12.41 s, PP 12.87 s, zastavené:
 *      52 57 3A 01 02 00 00  7A 30 00 82  46 32 00 82  00 00 00 00  00 00 00 00
 *      00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  0D
 *    Resetnutá: stopky čas 0; odpočet T1 = nastavený čas (240000 ms = 4:00), stav 0x00
 *  PRÍKAZY (USB, RS232 aj HC-12, ukončené CR): "RW:RST" = RESET, "RW:CHNG" = CHANGE
 *  USB: 115200 baud. Ladiace texty na USB sú vypnuté (usb_debug=1 ich zapne,
 *       ale vtedy sa na USB neposielajú rámce).
 *  Nastavenie udp_text=1 na SD karte prepne UDP na čitateľný text:
 *    $FT,<rezim>,<stav R/B/S/M>,<hlavny_cas>,<LP>,<PP>,<zobrazenie>*<XOR>
 ******************************************************************************/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>   // interná pamäť ESP32 (EEPROM / NVS)
#include <StreamString.h>
#include "esp_timer.h"
#include "esp_wifi.h"
#if __has_include("esp_wifi_ap_get_sta_list.h")
  #include "esp_wifi_ap_get_sta_list.h"
  #define HAVE_STA_LIST 1      // vieme zistiť IP všetkých pripojených zariadení
#endif

// ============================================================================
//  PINY  (ESP32 DevKit)
// ============================================================================
#define PIN_IN_LP        34   // vstup 1 – iba vstup, POTREBNÝ externý pull-up 4k7 na 3V3
#define PIN_IN_PP        35   // vstup 2 – iba vstup, POTREBNÝ externý pull-up 4k7 na 3V3
#define PIN_IN_START     32   // vstup 3
#define PIN_IN_CHANGE    33   // vstup 4
#define PIN_IN_RESET     27   // vstup 5

#define PIN_RELE_T1      25   // LED terč LP
#define PIN_RELE_T2      26   // LED terč PP

#define PIN_I2C_SDA      21
#define PIN_I2C_SCL      22
#define I2C_CLOCK        400000UL   // ak LCD zobrazuje nezmysly, zmeň na 100000UL

#define PIN_SD_SCK       18
#define PIN_SD_MISO      19
#define PIN_SD_MOSI      23
#define PIN_SD_CS         5

#define PIN_RS232_RX     16   // Serial2 <- MAX3232
#define PIN_RS232_TX     17   // Serial2 -> MAX3232
#define PIN_HC12_RX      14   // Serial1 <- TXD HC-12
#define PIN_HC12_TX      13   // Serial1 -> RXD HC-12
#define PIN_HC12_SET      4   // HIGH = normálny režim HC-12

#define INPUT_ACTIVE_LOW  1   // 1 = optočlen stiahne vstup na GND (odporúčané)

#define CFG_FILE         "/casomiera.cfg"
#define LONG_PRESS_MS    2000
#define MENU_ITEMS       5
#define BOOT_INFO_MS     8000        // ako dlho ukazuje LCD po štarte WiFi údaje
#define MAX_TIME_CS      99999UL     // 999.99 s – automatické zastavenie

// ============================================================================
//  PREDVOLENÉ NASTAVENIA
//  Použijú sa, ak SD karta chýba alebo sa súbor nepodarí načítať.
//  Ak je SD karta vložená a súbor neexistuje, vytvorí sa s týmito hodnotami.
// ============================================================================
#define DEF_MODE              1          // 1 = LP+PP, 2 = LP, 3 = odpočet
// LED terčov:  1 = svetlo napája niečo iné (relé sa nepoužíva)
//              0 = relé zopne, keď sa daný čas (LP / PP) zastaví
#define DEF_LED_T1            false      // T1 = LP = LED1
#define DEF_LED_T2            false      // T2 = PP = LED2
#define DEF_RELAY_ACTIVE_LOW  true       // väčšina relé modulov spína LOW
#define DEF_MIN_RUN_MS        1000       // zostrelenie skôr ako X ms po štarte sa ignoruje
#define DEF_COUNTDOWN_MIN     4          // odpočet 1–9 minút
#define DEF_COUNTDOWN_S       (DEF_COUNTDOWN_MIN * 60)
#define DEF_DEBOUNCE_MS       30         // ošetrenie zákmitov tlačidiel 4 a 5
#define DEF_NET_INTERVAL_MS   100        // UDP 10x za sekundu
#define DEF_SER_INTERVAL_MS   100        // RS232 + HC-12 10x za sekundu
#define DEF_LCD_INTERVAL_MS   100        // LCD 10x za sekundu
#define DEF_WIFI_AP           true       // true = ESP32 vytvorí vlastnú WiFi
#define DEF_SSID              "Casomiera"
#define DEF_PASS              "hasici123" // min. 8 znakov
#define DEF_PHONE_IP          "broadcast" // alebo konkrétna IP, napr. 192.168.4.2
#define DEF_PHONE_PORT        5000
#define DEF_DISP_IP           "broadcast" // IP druhého ESP32 s LED displejom
#define DEF_DISP_PORT         5001
#define DEF_RS232_BAUD        9600
#define DEF_HC12_BAUD         9600
#define DEF_USB_BAUD          115200     // USB (COM port ESP32) – rovnako nastav v PC programe
#define DEF_LCD_ADDR          0x27       // niekedy 0x3F

struct Config {
  uint8_t  mode           = DEF_MODE;
  bool     ledT1          = DEF_LED_T1;
  bool     ledT2          = DEF_LED_T2;
  bool     relayActiveLow = DEF_RELAY_ACTIVE_LOW;
  uint32_t minRunMs       = DEF_MIN_RUN_MS;
  uint32_t countdownS     = DEF_COUNTDOWN_S;
  uint32_t debounceMs     = DEF_DEBOUNCE_MS;
  uint32_t netIntervalMs  = DEF_NET_INTERVAL_MS;
  uint32_t serIntervalMs  = DEF_SER_INTERVAL_MS;
  uint32_t lcdIntervalMs  = DEF_LCD_INTERVAL_MS;
  bool     wifiAP         = DEF_WIFI_AP;
  String   ssid           = DEF_SSID;
  String   pass           = DEF_PASS;
  String   phoneIP        = DEF_PHONE_IP;
  uint16_t phonePort      = DEF_PHONE_PORT;
  String   dispIP         = DEF_DISP_IP;
  uint16_t dispPort       = DEF_DISP_PORT;
  uint32_t rs232Baud      = DEF_RS232_BAUD;
  uint32_t hc12Baud       = DEF_HC12_BAUD;
  uint8_t  lcdAddr        = DEF_LCD_ADDR;
  bool     useUDP         = true;
  bool     useRS232       = true;
  bool     useHC12        = true;
  bool     udpText        = false;   // false = UDP rovnaký binárny rámec ako RS232
  bool     useUSB         = true;    // rámec RW: aj cez USB (COM port ESP32, CP210x)
  bool     usbDebug       = false;   // true = USB vypisuje ladiace texty namiesto rámcov
  uint32_t usbBaud        = DEF_USB_BAUD;
} cfg;

// Ladiace výpisy – idú na USB len keď usb_debug=1 (inak by kazili rámce pre PC program)
class DebugOut : public Print {
public:
  size_t write(uint8_t c) override { return cfg.usbDebug ? Serial.write(c) : 1; }
  size_t write(const uint8_t *b, size_t n) override { return cfg.usbDebug ? Serial.write(b, n) : n; }
};
DebugOut Dbg;

// ============================================================================
//  GLOBÁLNE PREMENNÉ
// ============================================================================
enum : uint8_t { ST_READY = 0, ST_RUN, ST_STOP, ST_MENU };

// Štruktúry musia byť pred prvou funkciou (Arduino IDE generuje prototypy)
struct Snap  { uint8_t st; int64_t ts, tl, tp; bool hl, hp; };
struct Times { uint8_t st; uint32_t mainCs, lpCs, ppCs; bool hl, hp; };
struct Button {
  uint8_t pin, idx;
  bool pressed;
  uint32_t pressMs;
  bool longFired, shortEv, longEv;
};
struct CmdRx { char buf[16]; uint8_t n; };   // príjem príkazov RW:RST / RW:CHNG

#define EV_START 0x01
#define EV_LP    0x02
#define EV_PP    0x04

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// zdieľané s prerušeniami
volatile uint8_t  gState    = ST_READY;
volatile uint8_t  gMode     = DEF_MODE;
volatile int64_t  gMinRunUs = (int64_t)DEF_MIN_RUN_MS * 1000LL;
volatile int64_t  gTStart = 0, gTLP = 0, gTPP = 0;
volatile bool     gHitLP = false, gHitPP = false;
volatile uint8_t  gEvents = 0;
volatile bool     gBtnEdge[2]   = { false, false };   // 0 = CHANGE, 1 = RESET
volatile uint32_t gBtnEdgeMs[2] = { 0, 0 };

// iba v loop()
int64_t   tStop        = 0;
uint8_t   viewIdx      = 0;      // zobrazovaný kanál: 0 = T1/LP, 1 = T2/PP
bool      viewManual   = false;  // true = vybrané tlačidlom CHANGE, inak sa ukazuje väčší čas
uint32_t  bootInfoUntil = 0;     // do kedy LCD ukazuje WiFi údaje
bool      countdownEnd = false;
bool      manLP = false, manPP = false;   // čas zastavený ručne tlačidlom RESET
bool      sdOK         = false;
uint8_t   wifiStatus   = 0;      // 0 = nič, 1 = AP, 2 = STA
bool      phoneBc = true, dispBc = true;
IPAddress phoneAddr, dispAddr;
bool      forceSend    = false;
uint8_t   menuIdx      = 0;
char      tmpMsg[21]   = "";
uint32_t  tmpMsgUntil  = 0;

LiquidCrystal_I2C *lcd = nullptr;
WiFiUDP udp;

// Odosielanie beží v samostatnej úlohe na jadre 0 (WiFi jadro).
// Loop (jadro 1) iba pripraví správu do buffra – nikdy nečaká na sieť ani sériovú linku.
portMUX_TYPE txMux = portMUX_INITIALIZER_UNLOCKED;
char      txBuf[96];              // binárny rámec RW: (RS232, HC-12, UDP)
size_t    txLen   = 0;
char      txText[96];             // textová správa (UDP, ak udp_text=1)
size_t    txTextLen = 0;
uint32_t  txEvent = 0;            // zvýši sa pri štarte / zostrelení / stope -> okamžité odoslanie
#define   MAX_CLIENTS 8
IPAddress clientIP[MAX_CLIENTS];  // pripojené zariadenia (mobil, LED displej...)
uint8_t   clientCnt = 0;
char lcdCache[4][21];

// ============================================================================
//  PRERUŠENIA  (každý vstup má vlastné)
// ============================================================================
void IRAM_ATTR isrStart() {
  int64_t t = esp_timer_get_time();
  portENTER_CRITICAL_ISR(&mux);
  if (gState == ST_READY) {
    gTStart = t;
    gHitLP = false;
    gHitPP = false;
    gState = ST_RUN;
    gEvents |= EV_START;
  }
  portEXIT_CRITICAL_ISR(&mux);
}

void IRAM_ATTR isrLP() {
  int64_t t = esp_timer_get_time();
  portENTER_CRITICAL_ISR(&mux);
  if (gState == ST_RUN && gMode != 3 && !gHitLP && (t - gTStart) >= gMinRunUs) {
    gTLP = t;                       // prvá hrana = čas, zákmity sa ignorujú
    gHitLP = true;
    gEvents |= EV_LP;
  }
  portEXIT_CRITICAL_ISR(&mux);
}

void IRAM_ATTR isrPP() {
  int64_t t = esp_timer_get_time();
  portENTER_CRITICAL_ISR(&mux);
  if (gState == ST_RUN && gMode == 1 && !gHitPP && (t - gTStart) >= gMinRunUs) {
    gTPP = t;
    gHitPP = true;
    gEvents |= EV_PP;
  }
  portEXIT_CRITICAL_ISR(&mux);
}

void IRAM_ATTR isrChange() {
  uint32_t m = (uint32_t)(esp_timer_get_time() / 1000);
  portENTER_CRITICAL_ISR(&mux);
  gBtnEdge[0] = true;
  gBtnEdgeMs[0] = m;
  portEXIT_CRITICAL_ISR(&mux);
}

void IRAM_ATTR isrReset() {
  uint32_t m = (uint32_t)(esp_timer_get_time() / 1000);
  portENTER_CRITICAL_ISR(&mux);
  gBtnEdge[1] = true;
  gBtnEdgeMs[1] = m;
  portEXIT_CRITICAL_ISR(&mux);
}

// ============================================================================
//  POMOCNÉ FUNKCIE – ČAS
// ============================================================================
Snap snap() {
  Snap s;
  portENTER_CRITICAL(&mux);
  s.st = gState; s.ts = gTStart; s.tl = gTLP; s.tp = gTPP;
  s.hl = gHitLP; s.hp = gHitPP;
  portEXIT_CRITICAL(&mux);
  return s;
}

uint32_t usToCs(int64_t d) {               // mikrosekundy -> stotiny (orezanie)
  if (d < 0) d = 0;
  uint64_t cs = (uint64_t)d / 10000ULL;
  if (cs > MAX_TIME_CS) cs = MAX_TIME_CS;
  return (uint32_t)cs;
}

void fmtCs(char *b, size_t n, uint32_t cs) {          // 12.87
  snprintf(b, n, "%lu.%02lu", (unsigned long)(cs / 100), (unsigned long)(cs % 100));
}

void fmtMin(char *b, size_t n, uint32_t cs) {         // 5:00:98  (min:sek:stotiny)
  uint32_t s = cs / 100;
  snprintf(b, n, "%lu:%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60),
           (unsigned long)(cs % 100));
}

Times getTimes() {
  Snap s = snap();
  Times t;
  t.st = s.st; t.hl = s.hl; t.hp = s.hp;
  int64_t now = esp_timer_get_time();

  uint32_t el = 0;
  if (s.st == ST_RUN)  el = usToCs(now - s.ts);
  if (s.st == ST_STOP) el = usToCs(tStop - s.ts);

  // nezastavený kanál ukazuje bežiaci čas
  t.lpCs = s.hl ? usToCs(s.tl - s.ts) : el;
  t.ppCs = s.hp ? usToCs(s.tp - s.ts) : el;

  if (cfg.mode == 3) {
    uint32_t tot = cfg.countdownS * 100UL;
    t.mainCs = (el >= tot) ? 0 : tot - el;
  } else {
    t.mainCs = (cfg.mode == 1 && viewIdx == 1) ? t.ppCs : t.lpCs;   // vybraný kanál
  }
  return t;
}

const char *modeName(uint8_t m) {
  return m == 1 ? "LP+PP" : (m == 2 ? "LP" : "ODPOCET");
}

void showTmp(const char *m) {
  strncpy(tmpMsg, m, 20);
  tmpMsg[20] = 0;
  tmpMsgUntil = millis() + 2000;
}

// ============================================================================
//  RELÉ
// ============================================================================
void setRelay(uint8_t pin, bool on) {
  digitalWrite(pin, (on ^ cfg.relayActiveLow) ? HIGH : LOW);
}

void updateRelays() {
  Snap s = snap();
  bool t1 = false, t2 = false;
  // relé sa používa len pre LED nastavenú na 0 (pri 1 svetlo napája niečo iné)
  bool use1 = !cfg.ledT1, use2 = !cfg.ledT2;
  if (s.st == ST_MENU) {                       // test v menu na položke LED
    t1 = (menuIdx == 2) && use1;
    t2 = (menuIdx == 3) && use2;
  } else if (cfg.mode == 3) {                  // koniec odpočtu
    t1 = countdownEnd && use1;
    t2 = countdownEnd && use2;
  } else {                                     // zopne, keď sa daný čas zastaví
    t1 = s.hl && use1;
    t2 = s.hp && use2 && cfg.mode == 1;
  }
  setRelay(PIN_RELE_T1, t1);
  setRelay(PIN_RELE_T2, t2);
}

// ============================================================================
//  RIADENIE BEHU
// ============================================================================
void doReset() {
  portENTER_CRITICAL(&mux);
  gState = ST_READY;
  gHitLP = false; gHitPP = false;
  gTStart = 0; gTLP = 0; gTPP = 0;
  gEvents = 0;
  portEXIT_CRITICAL(&mux);
  tStop = 0;
  viewIdx = 0;
  countdownEnd = false;
  manLP = false;
  manPP = false;
  viewManual = false;
  forceSend = true;
  Dbg.println(F("[RESET]"));
}

// RESET počas behu: zastaví časy, ktoré ešte bežia (nezostrelené terče / odpočet).
// Ďalšie stlačenie RESET až vynuluje časomieru.
void manualStop() {
  int64_t now = esp_timer_get_time();
  bool stopped = false;
  portENTER_CRITICAL(&mux);
  if (gState == ST_RUN) {
    if (gMode != 3) {
      if (!gHitLP) { gTLP = now; gHitLP = true; manLP = true; }
      if (gMode == 1 && !gHitPP) { gTPP = now; gHitPP = true; manPP = true; }
    }
    gState = ST_STOP;
    stopped = true;
  }
  portEXIT_CRITICAL(&mux);
  if (!stopped) return;

  tStop = now;          // = čas posledného (práve zastaveného) prúdu
  viewManual = false;
  forceSend = true;
  char a[16];
  fmtCs(a, sizeof a, usToCs(tStop - gTStart));
  Dbg.printf("[STOP RUCNE] %s s%s%s\n", a, manLP ? " LP" : "", manPP ? " PP" : "");
}

// zobrazovaný kanál: ak ho nevybral CHANGE, vždy ten s väčším časom
void updateView() {
  if (cfg.mode != 1) { viewIdx = 0; return; }
  if (viewManual) return;
  Times t = getTimes();
  uint8_t v = (t.ppCs > t.lpCs) ? 1 : 0;
  if (v != viewIdx) { viewIdx = v; forceSend = true; }
}

void processRun() {
  uint8_t ev;
  portENTER_CRITICAL(&mux);
  ev = gEvents; gEvents = 0;
  portEXIT_CRITICAL(&mux);

  if (ev) {
    forceSend = true;
    Times t = getTimes();
    char a[16];
    if (ev & EV_START) Dbg.println(F("[START]"));
    if (ev & EV_LP) { fmtCs(a, sizeof a, t.lpCs); Dbg.printf("[LP] %s s\n", a); }
    if (ev & EV_PP) { fmtCs(a, sizeof a, t.ppCs); Dbg.printf("[PP] %s s\n", a); }
  }

  Snap s = snap();
  if (s.st != ST_RUN) return;

  int64_t now = esp_timer_get_time();
  bool stop = false;
  int64_t ts = 0;

  if (cfg.mode == 3) {
    int64_t end = s.ts + (int64_t)cfg.countdownS * 1000000LL;
    if (now >= end) { stop = true; ts = end; countdownEnd = true; }
  } else if (cfg.mode == 1) {
    if (s.hl && s.hp) { stop = true; ts = (s.tl > s.tp) ? s.tl : s.tp; }
  } else {
    if (s.hl) { stop = true; ts = s.tl; }
  }

  if (!stop && (now - s.ts) >= (int64_t)MAX_TIME_CS * 10000LL) {
    stop = true;
    ts = s.ts + (int64_t)MAX_TIME_CS * 10000LL;
  }

  if (stop) {
    tStop = ts;
    viewManual = false;   // po zastavení ukáž väčší čas
    portENTER_CRITICAL(&mux);
    if (gState == ST_RUN) gState = ST_STOP;
    portEXIT_CRITICAL(&mux);
    forceSend = true;
    char a[16];
    fmtCs(a, sizeof a, usToCs(tStop - s.ts));
    Dbg.printf("[STOP] %s s\n", a);
  }
}

// ============================================================================
//  TLAČIDLÁ 4 a 5 (prerušenie zachytí hranu, loop ju ošetrí od zákmitov)
// ============================================================================
Button btnChange = { PIN_IN_CHANGE, 0, false, 0, false, false, false };
Button btnReset  = { PIN_IN_RESET,  1, false, 0, false, false, false };

bool inputActive(uint8_t pin) {
  return digitalRead(pin) == (INPUT_ACTIVE_LOW ? LOW : HIGH);
}

void updateButton(Button &b) {
  bool doRead = false;
  uint32_t now;
  portENTER_CRITICAL(&mux);
  now = millis();
  if (gBtnEdge[b.idx] && (now - gBtnEdgeMs[b.idx]) >= cfg.debounceMs) {
    gBtnEdge[b.idx] = false;
    doRead = true;
  }
  portEXIT_CRITICAL(&mux);

  if (doRead) {
    bool p = inputActive(b.pin);
    if (p != b.pressed) {
      b.pressed = p;
      if (p) { b.pressMs = now; b.longFired = false; }
      else if (!b.longFired) b.shortEv = true;     // krátke stlačenie = pri pustení
    }
  }
  if (b.pressed && !b.longFired && (millis() - b.pressMs) >= LONG_PRESS_MS) {
    b.longFired = true;
    b.longEv = true;
  }
}

bool saveConfig();   // dopredná deklarácia

void enterMenu() {
  portENTER_CRITICAL(&mux);
  gState = ST_MENU;
  portEXIT_CRITICAL(&mux);
  menuIdx = 0;
  forceSend = true;
}

void exitMenu() {
  gMode = cfg.mode;
  if (saveConfig()) showTmp(sdOK ? "Ulozene na SD" : "Ulozene do EEPROM");
  else              showTmp("Chyba ukladania!");
  doReset();
}

// príkazy z PC programu (ako originál): "RW:RST\r" = RESET, "RW:CHNG\r" = CHANGE
CmdRx rxRS232 = { {0}, 0 }, rxHC12 = { {0}, 0 }, rxUSB = { {0}, 0 };

void pollCmd(HardwareSerial &port, CmdRx &r) {
  while (port.available()) {
    char c = (char)port.read();
    if (c == '\r' || c == '\n') {
      r.buf[r.n] = 0;
      if (strcmp(r.buf, "RW:RST") == 0)       { btnReset.shortEv = true;  Dbg.println(F("[CMD] RST")); }
      else if (strcmp(r.buf, "RW:CHNG") == 0) { btnChange.shortEv = true; Dbg.println(F("[CMD] CHNG")); }
      r.n = 0;
    } else if (r.n < sizeof(r.buf) - 1) {
      r.buf[r.n++] = c;
    } else {
      r.n = 0;   // príliš dlhý riadok – zahodiť
    }
  }
}

void readCommands() {
  if (cfg.useRS232) pollCmd(Serial2, rxRS232);
  if (cfg.useHC12)  pollCmd(Serial1, rxHC12);
  if (cfg.useUSB)   pollCmd(Serial,  rxUSB);
}

void handleButtons() {
  updateButton(btnChange);
  updateButton(btnReset);
  uint8_t st = snap().st;

  // ľubovoľné tlačidlo zavrie úvodnú obrazovku s WiFi údajmi
  if (bootInfoUntil && (btnChange.shortEv || btnReset.shortEv || btnChange.longEv)) {
    bootInfoUntil = 0;
    btnChange.shortEv = btnReset.shortEv = btnChange.longEv = btnReset.longEv = false;
    return;
  }

  if (st == ST_MENU) {
    if (btnChange.shortEv) menuIdx = (menuIdx + 1) % MENU_ITEMS;
    if (btnReset.shortEv) {
      switch (menuIdx) {
        case 0: cfg.mode = cfg.mode % 3 + 1; gMode = cfg.mode; break;
        case 1: cfg.countdownS = ((cfg.countdownS / 60) % 9 + 1) * 60; break;   // 1..9 min
        case 2: cfg.ledT1 = !cfg.ledT1; break;
        case 3: cfg.ledT2 = !cfg.ledT2; break;
        case 4: exitMenu(); break;
      }
    }
    if (btnChange.longEv) exitMenu();
  } else {
    if (btnReset.shortEv) {
      if (st == ST_RUN) manualStop();   // 1. stlačenie: zastaví bežiace časy
      else              doReset();      // 2. stlačenie: vynulovanie
    }
    if (btnChange.shortEv && st != ST_READY && cfg.mode == 1) {   // listovanie 0 <-> 1
      viewIdx ^= 1;
      viewManual = true;
      forceSend = true;
    }
    if (btnChange.longEv && st != ST_RUN) enterMenu();
  }

  btnChange.shortEv = btnChange.longEv = false;
  btnReset.shortEv  = btnReset.longEv  = false;
}

// ============================================================================
//  ODOSIELANIE  (UDP telefón, UDP LED displej, RS232, HC-12)
// ============================================================================
char stateChar(uint8_t st) {
  switch (st) {
    case ST_READY: return 'R';
    case ST_RUN:   return 'B';
    case ST_STOP:  return 'S';
    default:       return 'M';
  }
}

size_t buildMsg(char *out, size_t n) {
  Times t = getTimes();
  char m[12], l[12], p[12], body[80];
  fmtCs(m, sizeof m, t.mainCs);
  if (t.hl) fmtCs(l, sizeof l, t.lpCs); else strcpy(l, "-");
  if (t.hp) fmtCs(p, sizeof p, t.ppCs); else strcpy(p, "-");
  snprintf(body, sizeof body, "FT,%u,%c,%s,%s,%s,%u",
           cfg.mode, stateChar(t.st), m, l, p, viewIdx);
  uint8_t cs = 0;
  for (const char *q = body; *q; q++) cs ^= (uint8_t)*q;
  int len = snprintf(out, n, "$%s*%02X\r\n", body, cs);
  return (len < 0) ? 0 : (size_t)len;
}

// ---------------------------------------------------------------------------
//  BINÁRNY RÁMEC ORIGINÁLNEJ ČASOMIERY  "RW:" ... 0x0D  (24 bajtov)
// ---------------------------------------------------------------------------
#define FR_LEN       40   // 23 dátových + rezerva + CR na konci (prijímač drží 40 bajtov)
#define TS_NULL      0x00   // nulový / nepoužitý
#define TS_RUN       0x81   // beží
#define TS_STOP      0x82   // zastavený senzorom
#define TS_FINISH    0x84   // ukončený ručne / koniec odpočtu

static void frPut(uint8_t *f, uint8_t ch, uint32_t ms, uint8_t st) {
  uint8_t b = 7 + ch * 4;
  f[b]     = ms & 0xFF;          // little-endian
  f[b + 1] = (ms >> 8) & 0xFF;
  f[b + 2] = (ms >> 16) & 0xFF;
  f[b + 3] = st;
}

// Prijímač (originálny PC program) končí rámec na prvom bajte 0x0D. Časy sú násobky
// 10 ms, takže najnižší bajt je vždy párny; 0x0D môže vzniknúť len v strednom bajte
// (napr. 3.33–3.58 s). Počas behu sa taký rámec vynechá (pošle sa predchádzajúci).
static size_t frameOk(const uint8_t *f, uint8_t st) {
  if (st != ST_RUN) return FR_LEN;
  for (uint8_t i = 3; i < FR_LEN - 1; i++) if (f[i] == 0x0D) return 0;
  return FR_LEN;
}

size_t buildFrame(uint8_t *f) {
  Snap s = snap();
  int64_t now = esp_timer_get_time();
  bool cd = (cfg.mode == 3);

  memset(f, 0, FR_LEN);
  f[0] = 'R'; f[1] = 'W'; f[2] = ':';
  f[3] = cd ? 2 : 1;                              // 1 = stopky, 2 = odpočet
  f[4] = (cfg.mode == 1) ? 2 : 1;                 // počet kanálov
  f[5] = viewIdx;                                 // zobrazovaný čas
  f[6] = (s.st == ST_READY || s.st == ST_MENU) ? 0x01 : 0x00;
  f[FR_LEN - 1] = 0x0D;

  if (s.st != ST_RUN && s.st != ST_STOP) {             // resetnutá / menu
    if (cd) frPut(f, 0, cfg.countdownS * 1000UL, TS_NULL); // odpočet: plný čas (4:00)
    return FR_LEN;                                          // stopky: čas 0
  }

  uint32_t elCs = (s.st == ST_RUN) ? usToCs(now - s.ts) : usToCs(tStop - s.ts);

  if (cd) {
    uint32_t tot = cfg.countdownS * 100UL;
    uint32_t rem = (elCs >= tot) ? 0 : tot - elCs;
    uint8_t  st  = (s.st == ST_RUN) ? TS_RUN : TS_FINISH;
    frPut(f, 0, rem * 10UL, st);
    return frameOk(f, s.st);
  }

  // T1 = LP
  if (s.hl) frPut(f, 0, usToCs(s.tl - s.ts) * 10UL, manLP ? TS_FINISH : TS_STOP);
  else      frPut(f, 0, elCs * 10UL, (s.st == ST_RUN) ? TS_RUN : TS_FINISH);

  // T2 = PP (len režim 1)
  if (cfg.mode == 1) {
    if (s.hp) frPut(f, 1, usToCs(s.tp - s.ts) * 10UL, manPP ? TS_FINISH : TS_STOP);
    else      frPut(f, 1, elCs * 10UL, (s.st == ST_RUN) ? TS_RUN : TS_FINISH);
  }
  return frameOk(f, s.st);
}

IPAddress bcastIP() {
  return (wifiStatus == 1) ? WiFi.softAPBroadcastIP() : WiFi.broadcastIP();
}

void udpSend(IPAddress ip, uint16_t port, const char *m, size_t len) {
  udp.beginPacket(ip, port);
  udp.write((const uint8_t *)m, len);
  udp.endPacket();
}

// LOOP (jadro 1): každých 10 ms (a hneď pri udalosti) uloží aktuálnu správu do buffra.
void publishMsg() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (!forceSend && (now - last) < 5) return;
  last = now;

  uint8_t fr[FR_LEN];
  if (!buildFrame(fr)) return;   // rámec s 0x0D v dátach počas behu – vynechať
  char txt[96];
  size_t tlen = cfg.udpText ? buildMsg(txt, sizeof txt) : 0;

  portENTER_CRITICAL(&txMux);
  memcpy(txBuf, fr, FR_LEN);
  txLen = FR_LEN;
  memcpy(txText, txt, tlen);
  txTextLen = tlen;
  if (forceSend) txEvent++;
  portEXIT_CRITICAL(&txMux);
  forceSend = false;
}

// zoznam IP adries zariadení pripojených na WiFi časomiery
void refreshClients() {
  clientCnt = 0;
#ifdef HAVE_STA_LIST
  wifi_sta_list_t sl;
  wifi_sta_mac_ip_list_t il;
  if (esp_wifi_ap_get_sta_list(&sl) != ESP_OK) return;
  if (esp_wifi_ap_get_sta_list_with_ip(&sl, &il) != ESP_OK) return;
  for (int i = 0; i < il.num && clientCnt < MAX_CLIENTS; i++) {
    uint32_t a = il.sta[i].ip.addr;
    if (a) clientIP[clientCnt++] = IPAddress(a);
  }
#endif
}

void sendUDP(const char *msg, size_t len) {
  if (!cfg.useUDP || !wifiStatus) return;

  // pevne zadané IP v nastaveniach majú prednosť
  if (!phoneBc) udpSend(phoneAddr, cfg.phonePort, msg, len);
  if (!dispBc)  udpSend(dispAddr,  cfg.dispPort,  msg, len);
  if (!phoneBc && !dispBc) return;

#ifdef HAVE_STA_LIST
  if (wifiStatus == 1) {
    // vlastná WiFi: priamo každému pripojenému zariadeniu (spoľahlivejšie ako broadcast,
    // mobily broadcast v úspornom režime často zahadzujú)
    for (uint8_t i = 0; i < clientCnt; i++) {
      if (phoneBc) udpSend(clientIP[i], cfg.phonePort, msg, len);
      if (dispBc && cfg.dispPort != cfg.phonePort) udpSend(clientIP[i], cfg.dispPort, msg, len);
    }
    return;
  }
#endif
  IPAddress bc = bcastIP();
  if (phoneBc) udpSend(bc, cfg.phonePort, msg, len);
  if (dispBc && cfg.dispPort != cfg.phonePort) udpSend(bc, cfg.dispPort, msg, len);
}

// ÚLOHA ODOSIELANIA (jadro 0) – UDP mobil, UDP LED displej, RS232, HC-12
void commTask(void *) {
  uint32_t lastNet = 0, lastSer = 0, lastCli = 0;
  uint32_t evNet = 0, evSer = 0;
  char msg[96], txt[96];
  for (;;) {
    size_t len, tlen;
    uint32_t ev;
    portENTER_CRITICAL(&txMux);
    len = txLen;
    memcpy(msg, txBuf, len);
    tlen = txTextLen;
    memcpy(txt, txText, tlen);
    ev = txEvent;
    portEXIT_CRITICAL(&txMux);

    uint32_t now = millis();
    if (now - lastCli >= 1000) { lastCli = now; refreshClients(); }

    if (len) {
      // pevný takt (každých 100 ms = 10x za sekundu), udalosť sa pošle navyše hneď
      bool tickNet = (now - lastNet) >= cfg.netIntervalMs;
      bool tickSer = (now - lastSer) >= cfg.serIntervalMs;
      if (tickNet) lastNet = ((now - lastNet) > 2 * cfg.netIntervalMs) ? now : lastNet + cfg.netIntervalMs;
      if (tickSer) lastSer = ((now - lastSer) > 2 * cfg.serIntervalMs) ? now : lastSer + cfg.serIntervalMs;

      if (ev != evNet || tickNet) {
        evNet = ev;
        if (cfg.udpText && tlen) sendUDP(txt, tlen);
        else                     sendUDP(msg, len);
      }
      if (ev != evSer || tickSer) {
        evSer = ev;
        // zápis len ak je miesto v buffri -> nikdy nečaká
        // TX buffer 1024 B, správa ~35 B každých 100 ms -> buffer sa nikdy nenaplní,
        // zápis len skopíruje dáta do buffra a hneď sa vráti
        if (cfg.useRS232) Serial2.write((const uint8_t *)msg, len);
        if (cfg.useHC12)  Serial1.write((const uint8_t *)msg, len);
        if (cfg.useUSB && !cfg.usbDebug) Serial.write((const uint8_t *)msg, len);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ============================================================================
//  LCD 20x4  (prepisujú sa iba zmenené znaky – rýchle)
// ============================================================================
void lcdLine(uint8_t row, const char *txt) {
  char b[21];
  snprintf(b, sizeof b, "%-20s", txt);
  int a = 0;
  while (a < 20 && b[a] == lcdCache[row][a]) a++;
  if (a == 20) return;
  int z = 19;
  while (z > a && b[z] == lcdCache[row][z]) z--;
  lcd->setCursor(a, row);
  for (int i = a; i <= z; i++) lcd->write((uint8_t)b[i]);
  memcpy(lcdCache[row], b, 21);
}

void drawMenu() {
  char ln[32];
  uint8_t first = (menuIdx < 4) ? 0 : menuIdx - 3;     // posúvanie – 5 položiek, 4 riadky
  for (uint8_t r = 0; r < 4; r++) {
    uint8_t i = first + r;
    char c = (i == menuIdx) ? '>' : ' ';
    switch (i) {
      case 0: snprintf(ln, sizeof ln, "%cRezim: %u %s", c, cfg.mode, modeName(cfg.mode)); break;
      case 1: snprintf(ln, sizeof ln, "%cOdpocet: %lu min", c, (unsigned long)(cfg.countdownS / 60)); break;
      case 2: snprintf(ln, sizeof ln, "%cLED1 LP: %s", c, cfg.ledT1 ? "1 EXTERNE" : "0 RELE"); break;
      case 3: snprintf(ln, sizeof ln, "%cLED2 PP: %s", c, cfg.ledT2 ? "1 EXTERNE" : "0 RELE"); break;
      default: snprintf(ln, sizeof ln, "%cULOZIT A ODIST", c); break;
    }
    lcdLine(r, ln);
  }
}

// úvodná obrazovka – ako sa pripojiť na WiFi časomiery
void drawWifiInfo() {
  char ln[40];
  if (wifiStatus == 2) {
    snprintf(ln, sizeof ln, "WiFi: %s", cfg.ssid.c_str());                    lcdLine(0, ln);
    lcdLine(1, "Pripojene ako klient");
    snprintf(ln, sizeof ln, "IP: %s", WiFi.localIP().toString().c_str());     lcdLine(2, ln);
  } else if (wifiStatus == 1) {
    snprintf(ln, sizeof ln, "WiFi: %s", WiFi.softAPSSID().c_str());           lcdLine(0, ln);
    snprintf(ln, sizeof ln, "Heslo: %s", cfg.wifiAP ? cfg.pass.c_str() : DEF_PASS); lcdLine(1, ln);
    snprintf(ln, sizeof ln, "IP: %s", WiFi.softAPIP().toString().c_str());    lcdLine(2, ln);
  } else {
    lcdLine(0, "WiFi: NEFUNGUJE");
    lcdLine(1, "");
    lcdLine(2, "");
  }
  snprintf(ln, sizeof ln, "UDP port %u / %u", cfg.phonePort, cfg.dispPort);
  lcdLine(3, ln);
}

void drawLCD() {
  if (!lcd) return;
  uint8_t st = snap().st;
  if (st == ST_MENU) { drawMenu(); return; }
  if (bootInfoUntil) {
    if (st == ST_READY && (int32_t)(bootInfoUntil - millis()) > 0) { drawWifiInfo(); return; }
    bootInfoUntil = 0;                       // čas vypršal alebo štart -> normálny displej
  }

  Times t = getTimes();
  char ln[40], a[16], b[16];

  const char *stTxt = (st == ST_READY) ? "PRIPRAVENE" : (st == ST_RUN) ? "BEZI" : "STOP";
  snprintf(ln, sizeof ln, "%u:%-8s%10s", cfg.mode, modeName(cfg.mode), stTxt);
  lcdLine(0, ln);

  // všetky časy vo formáte min:sek:stotiny, napr. 5:00:98
  if (cfg.mode == 3) {
    fmtMin(a, sizeof a, t.mainCs);
    snprintf(ln, sizeof ln, "ODPOCET: %11s", a);
    lcdLine(1, ln);
    snprintf(ln, sizeof ln, "Odpocet z %lu min", (unsigned long)(cfg.countdownS / 60));
    lcdLine(2, ln);
  } else {
    const char *lab = (cfg.mode == 1 && viewIdx == 1) ? "PP:" : "LP:";
    fmtMin(a, sizeof a, t.mainCs);
    snprintf(ln, sizeof ln, "%-4s%c%15s", lab, viewManual ? '*' : ' ', a);
    lcdLine(1, ln);

    fmtMin(a, sizeof a, t.lpCs);
    if (cfg.mode == 1) {
      fmtMin(b, sizeof b, t.ppCs);
      snprintf(ln, sizeof ln, "LP%-8sPP%s", a, b);
    } else {
      snprintf(ln, sizeof ln, "LP %s", a);
    }
    lcdLine(2, ln);
  }

  if (tmpMsg[0] && (int32_t)(tmpMsgUntil - millis()) > 0) {
    lcdLine(3, tmpMsg);
  } else {
    tmpMsg[0] = 0;
    const char *ws = (wifiStatus == 1) ? "AP" : (wifiStatus == 2) ? "ST" : "--";
    snprintf(ln, sizeof ln, "W:%s %s REL:%c%c", ws, sdOK ? "SD" : "EE",
             cfg.ledT1 ? '-' : '1', cfg.ledT2 ? '-' : '2');
    lcdLine(3, ln);
  }
}

void lcdMsg(const char *l0, const char *l1) {
  if (!lcd) return;
  lcdLine(0, l0);
  lcdLine(1, l1);
  lcdLine(2, "");
  lcdLine(3, "");
}

// ============================================================================
//  SD KARTA – NASTAVENIA  (súbor /casomiera.cfg, formát kluc=hodnota)
// ============================================================================
bool parseBool(String v) {
  v.toLowerCase();
  return v == "1" || v == "true" || v == "ano" || v == "zap" || v == "on";
}

void applyKV(String k, String v) {
  k.toLowerCase();
  if      (k == "mode")             cfg.mode = constrain(v.toInt(), 1, 3);
  else if (k == "led_t1")           cfg.ledT1 = parseBool(v);
  else if (k == "led_t2")           cfg.ledT2 = parseBool(v);
  else if (k == "relay_active_low") cfg.relayActiveLow = parseBool(v);
  else if (k == "min_run_ms")       cfg.minRunMs = v.toInt();
  else if (k == "countdown_min")    cfg.countdownS = constrain(v.toInt(), 1, 9) * 60;
  else if (k == "countdown_s")      cfg.countdownS = constrain(v.toInt() / 60, 1, 9) * 60;
  else if (k == "debounce_ms")      cfg.debounceMs = constrain(v.toInt(), 5, 500);
  else if (k == "net_interval_ms")  cfg.netIntervalMs = constrain(v.toInt(), 20, 100);
  else if (k == "ser_interval_ms")  cfg.serIntervalMs = constrain(v.toInt(), 20, 100);
  else if (k == "lcd_interval_ms")  cfg.lcdIntervalMs = constrain(v.toInt(), 50, 500);
  else if (k == "wifi_mode")        { v.toUpperCase(); cfg.wifiAP = (v != "STA"); }
  else if (k == "ssid")             cfg.ssid = v;
  else if (k == "pass")             cfg.pass = v;
  else if (k == "phone_ip")         cfg.phoneIP = v;
  else if (k == "phone_port")       cfg.phonePort = v.toInt();
  else if (k == "disp_ip")          cfg.dispIP = v;
  else if (k == "disp_port")        cfg.dispPort = v.toInt();
  else if (k == "rs232_baud")       cfg.rs232Baud = v.toInt();
  else if (k == "hc12_baud")        cfg.hc12Baud = v.toInt();
  else if (k == "lcd_addr")         cfg.lcdAddr = (uint8_t)strtol(v.c_str(), nullptr, 0);
  else if (k == "use_udp")          cfg.useUDP = parseBool(v);
  else if (k == "use_rs232")        cfg.useRS232 = parseBool(v);
  else if (k == "use_hc12")         cfg.useHC12 = parseBool(v);
  else if (k == "udp_text")         cfg.udpText = parseBool(v);
  else if (k == "use_usb")          cfg.useUSB = parseBool(v);
  else if (k == "usb_debug")        cfg.usbDebug = parseBool(v);
  else if (k == "usb_baud")         cfg.usbBaud = v.toInt();
}

// zapíše všetky nastavenia ako text kluc=hodnota (rovnaký formát pre SD aj EEPROM)
void writeConfig(Print &f) {
  f.println("# Casomiera - nastavenia. Format: kluc=hodnota, # = komentar");
  f.println("# mode: 1 = LP+PP, 2 = LP, 3 = odpocet");
  f.printf("mode=%u\n", cfg.mode);
  f.println("# LED terca: 1 = napaja nieco ine (rele sa nepouziva), 0 = rele zopne po zastaveni casu");
  f.printf("led_t1=%u\n", cfg.ledT1);
  f.printf("led_t2=%u\n", cfg.ledT2);
  f.printf("relay_active_low=%u\n", cfg.relayActiveLow);
  f.printf("min_run_ms=%lu\n", (unsigned long)cfg.minRunMs);
  f.println("# odpocet 1-9 minut");
  f.printf("countdown_min=%lu\n", (unsigned long)(cfg.countdownS / 60));
  f.printf("debounce_ms=%lu\n", (unsigned long)cfg.debounceMs);
  f.println("# intervaly odosielania 20-100 ms (min. 10x za sekundu)");
  f.printf("net_interval_ms=%lu\n", (unsigned long)cfg.netIntervalMs);
  f.printf("ser_interval_ms=%lu\n", (unsigned long)cfg.serIntervalMs);
  f.printf("lcd_interval_ms=%lu\n", (unsigned long)cfg.lcdIntervalMs);
  f.println("# wifi_mode: AP = vlastna siet, STA = pripojit sa k sieti");
  f.printf("wifi_mode=%s\n", cfg.wifiAP ? "AP" : "STA");
  f.printf("ssid=%s\n", cfg.ssid.c_str());
  f.printf("pass=%s\n", cfg.pass.c_str());
  f.println("# IP: broadcast alebo konkretna adresa");
  f.printf("phone_ip=%s\n", cfg.phoneIP.c_str());
  f.printf("phone_port=%u\n", cfg.phonePort);
  f.printf("disp_ip=%s\n", cfg.dispIP.c_str());
  f.printf("disp_port=%u\n", cfg.dispPort);
  f.printf("rs232_baud=%lu\n", (unsigned long)cfg.rs232Baud);
  f.printf("hc12_baud=%lu\n", (unsigned long)cfg.hc12Baud);
  f.printf("lcd_addr=0x%02X\n", cfg.lcdAddr);
  f.printf("use_udp=%u\n", cfg.useUDP);
  f.printf("use_rs232=%u\n", cfg.useRS232);
  f.printf("use_hc12=%u\n", cfg.useHC12);
  f.println("# udp_text: 0 = UDP binarny ramec RW: (ako RS232), 1 = citatelny text $FT");
  f.printf("udp_text=%u\n", cfg.udpText);
  f.println("# USB (COM port ESP32): use_usb=1 posiela ramce RW:, usb_debug=1 posiela texty");
  f.printf("use_usb=%u\n", cfg.useUSB);
  f.printf("usb_debug=%u\n", cfg.usbDebug);
  f.printf("usb_baud=%lu\n", (unsigned long)cfg.usbBaud);
}

Preferences prefs;

// EEPROM (NVS) – použije sa, keď SD karta nie je alebo sa z nej nedá čítať
bool saveEeprom() {
  StreamString txt;
  writeConfig(txt);
  if (!prefs.begin("casomiera", false)) return false;
  size_t w = prefs.putString("cfg", txt);
  prefs.end();
  return w == txt.length();
}

bool saveSD() {
  SD.remove(CFG_FILE);
  File f = SD.open(CFG_FILE, FILE_WRITE);
  if (!f) return false;
  writeConfig(f);
  f.close();
  return true;
}

// SD karta má prednosť; bez SD sa ukladá do EEPROM
bool saveConfig() {
  if (sdOK && saveSD()) return true;
  return saveEeprom();
}

void parseLine(String line) {
  line.trim();
  if (line.length() == 0 || line[0] == '#') return;
  int e = line.indexOf('=');
  if (e < 1) return;
  String k = line.substring(0, e);
  String v = line.substring(e + 1);
  k.trim(); v.trim();
  applyKV(k, v);
}

bool loadEeprom() {
  if (!prefs.begin("casomiera", true)) return false;
  String txt = prefs.getString("cfg", "");
  prefs.end();
  if (txt.length() == 0) return false;
  int a = 0;
  while (a < (int)txt.length()) {
    int b = txt.indexOf('\n', a);
    if (b < 0) b = txt.length();
    parseLine(txt.substring(a, b));
    a = b + 1;
  }
  return true;
}

uint8_t cfgSource = 0;   // 0 = predvolené v programe, 1 = SD karta, 2 = EEPROM

void loadConfig() {
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  sdOK = SD.begin(PIN_SD_CS, SPI, 4000000);

  // 1) SD karta má prednosť
  if (sdOK && SD.exists(CFG_FILE)) {
    File f = SD.open(CFG_FILE, FILE_READ);
    if (f) {
      while (f.available()) parseLine(f.readStringUntil('\n'));
      f.close();
      cfgSource = 1;
      Dbg.println(F("Nastavenia: SD karta"));
    }
  }

  // 2) SD nie je / súbor sa nedal prečítať -> EEPROM
  if (cfgSource == 0 && loadEeprom()) {
    cfgSource = 2;
    Dbg.println(F("Nastavenia: EEPROM"));
  }

  // 3) inak predvolené hodnoty z programu
  if (cfgSource == 0) Dbg.println(F("Nastavenia: predvolene v programe"));

  if (cfg.pass.length() < 8) cfg.pass = DEF_PASS;

  // SD karta bez súboru -> vytvorí ho z aktuálnych nastavení
  if (sdOK && !SD.exists(CFG_FILE)) {
    if (saveSD()) Dbg.println(F("SD: vytvoreny subor s nastaveniami"));
  }
  // bez SD a bez EEPROM -> uloží predvolené do EEPROM
  if (!sdOK && cfgSource == 0) saveEeprom();
}

// ============================================================================
//  WIFI
// ============================================================================
void setupWiFi() {
  WiFi.persistent(false);
  wifiStatus = 0;

  if (!cfg.wifiAP) {
    lcdMsg("Pripajam WiFi...", cfg.ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) delay(100);
    if (WiFi.status() == WL_CONNECTED) {
      wifiStatus = 2;
      Dbg.print(F("WiFi STA IP: ")); Dbg.println(WiFi.localIP());
    } else {
      Dbg.println(F("WiFi STA zlyhalo -> spustam AP"));
      WiFi.disconnect(true);
    }
  }
  if (wifiStatus == 0) {   // vlastná sieť (aj ako záloha pri zlyhaní STA)
    WiFi.mode(WIFI_AP);
    const char *ss = cfg.wifiAP ? cfg.ssid.c_str() : DEF_SSID;
    const char *pw = cfg.wifiAP ? cfg.pass.c_str() : DEF_PASS;
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                      IPAddress(255, 255, 255, 0));
    if (WiFi.softAP(ss, pw, 1, 0, MAX_CLIENTS)) {   // kanál 1, viditeľná, max 8 zariadení
      wifiStatus = 1;
      Dbg.print(F("WiFi AP: ")); Dbg.print(ss);
      Dbg.print(F("  IP: ")); Dbg.println(WiFi.softAPIP());
    }
  }
  WiFi.setSleep(false);   // nižšia latencia

  phoneBc = !phoneAddr.fromString(cfg.phoneIP);
  dispBc  = !dispAddr.fromString(cfg.dispIP);
  udp.begin(cfg.phonePort);
}

// ============================================================================
//  SETUP / LOOP
// ============================================================================
void setup() {
  Serial.setTxBufferSize(1024);
  Serial.begin(DEF_USB_BAUD);
  delay(200);
  Dbg.println(F("\n=== CASOMIERA HASICSKY SPORT ==="));

  // relé hneď vypnúť
  pinMode(PIN_RELE_T1, OUTPUT);
  pinMode(PIN_RELE_T2, OUTPUT);
  setRelay(PIN_RELE_T1, false);
  setRelay(PIN_RELE_T2, false);

  // HC-12 normálny režim
  pinMode(PIN_HC12_SET, OUTPUT);
  digitalWrite(PIN_HC12_SET, HIGH);

  // vstupy (34 a 35 nemajú interný pull-up -> externý 4k7 je nutný)
  pinMode(PIN_IN_LP, INPUT);
  pinMode(PIN_IN_PP, INPUT);
  pinMode(PIN_IN_START, INPUT_PULLUP);
  pinMode(PIN_IN_CHANGE, INPUT_PULLUP);
  pinMode(PIN_IN_RESET, INPUT_PULLUP);

  // nastavenia (SD alebo predvolené)
  loadConfig();
  if (cfg.usbBaud != DEF_USB_BAUD) Serial.updateBaudRate(cfg.usbBaud);
  gMode = cfg.mode;
  gMinRunUs = (int64_t)cfg.minRunMs * 1000LL;
  setRelay(PIN_RELE_T1, false);
  setRelay(PIN_RELE_T2, false);

  // LCD
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  lcd = new LiquidCrystal_I2C(cfg.lcdAddr, 20, 4);
  lcd->init();
  Wire.setClock(I2C_CLOCK);
  lcd->backlight();
  lcd->clear();
  for (uint8_t r = 0; r < 4; r++) { memset(lcdCache[r], ' ', 20); lcdCache[r][20] = 0; }
  lcdMsg("CASOMIERA v1.1", cfgSource == 1 ? "Nastavenia: SD" :
                           cfgSource == 2 ? "Nastavenia: EEPROM" : "Nastavenia: predvol.");

  // sériové linky
  Serial2.setTxBufferSize(1024);
  Serial2.begin(cfg.rs232Baud, SERIAL_8N1, PIN_RS232_RX, PIN_RS232_TX);
  Serial1.setTxBufferSize(1024);
  Serial1.begin(cfg.hc12Baud, SERIAL_8N1, PIN_HC12_RX, PIN_HC12_TX);
  // test pri štarte – tento riadok musí prísť na RS232 aj HC-12 hneď po zapnutí
  Serial2.print("CASOMIERA RS232 OK\r");
  Serial1.print("CASOMIERA HC12 OK\r");
  Dbg.printf("RS232: TX=GPIO%d RX=GPIO%d %lu baud 8N1\n", PIN_RS232_TX, PIN_RS232_RX,
                (unsigned long)cfg.rs232Baud);

  // WiFi + UDP
  setupWiFi();

  // úloha odosielania na jadre 0 (loop a meranie bežia na jadre 1)
  xTaskCreatePinnedToCore(commTask, "comm", 4096, nullptr, 1, nullptr, 0);

  // prerušenia – každý vstup samostatne
  int edge = INPUT_ACTIVE_LOW ? FALLING : RISING;
  attachInterrupt(digitalPinToInterrupt(PIN_IN_LP),     isrLP,     edge);
  attachInterrupt(digitalPinToInterrupt(PIN_IN_PP),     isrPP,     edge);
  attachInterrupt(digitalPinToInterrupt(PIN_IN_START),  isrStart,  edge);
  attachInterrupt(digitalPinToInterrupt(PIN_IN_CHANGE), isrChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_IN_RESET),  isrReset,  CHANGE);

  doReset();
  bootInfoUntil = millis() + BOOT_INFO_MS;   // LCD najprv ukáže WiFi, heslo a IP
  if (!bootInfoUntil) bootInfoUntil = 1;
  Dbg.printf("Rezim: %u (%s)\n", cfg.mode, modeName(cfg.mode));
}

void loop() {
  processRun();      // vyhodnotenie stopky
  updateView();      // zobrazovaný čas: väčší, alebo vybraný tlačidlom CHANGE
  readCommands();    // RW:RST / RW:CHNG z RS232 a HC-12
  handleButtons();   // tlačidlá 4 a 5, menu
  updateRelays();    // LED terče
  publishMsg();      // pripraví správu – odosiela ju úloha commTask na jadre 0

  static uint32_t lastLcd = 0;
  if (millis() - lastLcd >= cfg.lcdIntervalMs) {
    lastLcd = millis();
    drawLCD();
  }
}
