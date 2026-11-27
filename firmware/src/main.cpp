// LoRa chat and friend finder between Heltec Wireless Tracker V1.1/V1.2 boards.
// The Mac app (firmware/chat) talks to the board over USB serial; the board relays over LoRa.
// Each board shares its GNSS position; the screen shows distance and an arrow to the other board.
// Pins from Meshtastic variants/esp32s3/heltec_wireless_tracker/variant.h.
//
// Serial, host to board:  INFO | SEND <n> <text> | READ <from> <n>
// Serial, board to host:  ID <id> | SENT <n> | DELIVERED <n> <rssi> <snr> | FAILED <n>
//                         MSG <from> <n> <rssi> <snr> <text> | READ <from> <n> | ERR <text>
//                         PRESET <n> <text>: the user sent a preset message with the button
// Location, host to board: FINDME | SETPOS <lat> <lon> | SETPOS off
// Location, board to host: GPS <fix 0|1|2> <sats> <lat> <lon> <kmh> <course> <hdop>
//                          PEER <id> <lat> <lon> <age_s> <dist_m> <bearing> <rssi> <snr> | FINDME <from> | FINDSENT
// Radio packets:          M <from> <n> <text> | A <acker> <origFrom> <n> | R <reader> <origFrom> <n>
//                         P <from> <lat> <lon> | F <from>
#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <Adafruit_ST7735.h>
#include <TinyGPSPlus.h>

// SX1262
#define LORA_CS 8
#define LORA_SCK 9
#define LORA_MOSI 10
#define LORA_MISO 11
#define LORA_RST 12
#define LORA_BUSY 13
#define LORA_DIO1 14

// ST7735S 80x160 TFT, powered from the Vext rail
#define TFT_CS 38
#define TFT_RST 39
#define TFT_DC 40
#define TFT_SCLK 41
#define TFT_MOSI 42
#define TFT_BL 21
#define VEXT_EN 3  // active HIGH on V1.1/V1.2, also powers the GNSS

// UC6580 GNSS on UART1, LED and USER button
#define GNSS_RX 33
#define GNSS_TX 34
#define GNSS_RST 35  // active LOW
#define LED_PIN 18
#define BUTTON_PIN 0  // USER button, LOW when pressed

// EU868 g3 sub-band (869.4-869.65 MHz, 10% duty cycle). Boards sit on a desk, so low power.
#define FREQ_MHZ 869.525
#define BW_KHZ 125.0
#define SF 7
#define CR 5
#define SYNC_WORD RADIOLIB_SX126X_SYNC_WORD_PRIVATE
#define TX_DBM 2

#define MAX_TEXT 180
#define OUTBOX 8
#define TRIES 4
#define ACK_WAIT_MS 1500

#define POS_EVERY_MS 15000   // a position packet is ~40 ms on air, so 0.3% duty cycle
#define REPORT_EVERY_MS 2000
#define PEER_STALE_S 120
#define NEARBY_M 15          // GNSS error is a few metres: below this an arrow misleads
#define WALKING_KMH 2.0      // above this, GNSS course tells which way the user faces
#define FLASH_MS 10000
#define LONG_PRESS_MS 800
#define MAX_PEERS 8
#define PRESET_N0 100000     // presets sent from the board count from here; the Mac app uses 1-65535

// Preset messages, picked with the USER button. At most 13 characters, so each fits one line at text size 2.
const char *PRESETS[] = {"Waar ben je?", "Kom naar mij", "Wacht op mij", "Alles goed", "Bij de bar", "Ga naar huis"};
const int NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

SPIClass loraSpi(FSPI);
SPIClass tftSpi(HSPI);
SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSpi);
Adafruit_ST7735 tft(&tftSpi, TFT_CS, TFT_DC, TFT_RST);
HardwareSerial gnssSerial(1);
TinyGPSPlus gps;

struct Peer {
  bool used;
  char id[5];
  double lat, lon;
  uint32_t at;
  float rssi, snr;
};
Peer peers[MAX_PEERS];

bool simPos = false;  // test position from SETPOS, for indoor testing
double simLat = 0, simLon = 0;
enum Screen { LOCATE, CHAT, PRESET } screen = LOCATE;
int preset = 0;                // which preset the PRESET screen shows
unsigned presetCount = 0;      // presets sent, for numbering
unsigned presetN = 0;          // number of the last preset sent
int presetSent = -1;           // which preset that was
const char *presetState = "";  // what happened to it, shown on that preset's screen
uint32_t flashUntil = 0, nextPosAt = 0, nextReportAt = 0, buttonDownAt = 0;
char flashFrom[5] = "";

struct Pending {
  bool active;
  unsigned n;
  uint8_t tries;
  uint32_t nextAt;
  String text;
};
Pending outbox[OUTBOX];

struct Seen {
  char from[5];
  unsigned n;
  uint32_t at;
};
Seen seen[16];
uint8_t seenNext = 0;

char myId[5];
uint32_t sentCount = 0, recvCount = 0;
float lastRssi = 0, lastSnr = 0;
String lastText = "";
char lastFrom[5] = "-";
String serialLine;
volatile bool rxFlag = false;

void IRAM_ATTR onDio1() { rxFlag = true; }

// Own position: 2 = test position, 1 = GNSS fix, 0 = unknown
int ownPos(double &lat, double &lon) {
  if (simPos) {
    lat = simLat;
    lon = simLon;
    return 2;
  }
  if (gps.location.isValid() && gps.location.age() < 5000) {
    lat = gps.location.lat();
    lon = gps.location.lng();
    return 1;
  }
  return 0;
}

const char *compassPoint(double deg) {
  static const char *names[] = {"N", "NO", "O", "ZO", "Z", "ZW", "W", "NW"};
  return names[(int)((deg + 22.5) / 45) % 8];
}

Peer *latestPeer() {
  Peer *best = nullptr;
  for (auto &p : peers)
    if (p.used && (!best || (int32_t)(p.at - best->at) > 0)) best = &p;
  return best;
}

void drawArrow(int cx, int cy, double deg, uint16_t color) {
  double a = deg * DEG_TO_RAD;
  auto px = [&](double r, double off) { return cx + (int)(r * sin(a + off)); };
  auto py = [&](double r, double off) { return cy - (int)(r * cos(a + off)); };
  tft.fillTriangle(px(30, 0), py(30, 0), px(20, 2.5), py(20, 2.5), px(8, PI), py(8, PI), color);
  tft.fillTriangle(px(30, 0), py(30, 0), px(20, -2.5), py(20, -2.5), px(8, PI), py(8, PI), color);
}

void drawLocate() {
  tft.fillScreen(ST77XX_BLACK);
  tft.drawCircle(40, 40, 36, ST77XX_WHITE);
  double lat, lon;
  int own = ownPos(lat, lon);
  Peer *p = latestPeer();
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(84, 0);
  if (!p) {
    tft.print("Geen vriend");
    tft.setCursor(84, 12);
    tft.print("in de buurt");
  } else {
    uint32_t age = (millis() - p->at) / 1000;
    tft.setTextColor(age > PEER_STALE_S ? ST77XX_RED : ST77XX_YELLOW);
    tft.printf("Vriend %s", p->id);
    if (own) {
      double dist = TinyGPSPlus::distanceBetween(lat, lon, p->lat, p->lon);
      double bearing = TinyGPSPlus::courseTo(lat, lon, p->lat, p->lon);
      tft.setTextColor(ST77XX_WHITE);
      tft.setTextSize(2);
      tft.setCursor(84, 12);
      if (dist < 1000) tft.printf("%.0f m", dist);
      else tft.printf("%.1fkm", dist / 1000);
      tft.setTextSize(1);
      tft.setCursor(84, 32);
      tft.printf("%.0f %s", bearing, compassPoint(bearing));
      bool walking = own == 1 && gps.speed.isValid() && gps.speed.kmph() > WALKING_KMH && gps.course.isValid();
      if (dist < NEARBY_M) {
        tft.setCursor(16, 36);
        tft.print("Dichtbij");
      } else if (walking) {
        drawArrow(40, 40, bearing - gps.course.deg(), ST77XX_GREEN);  // screen top = walking direction
        tft.setCursor(2, 0);
        tft.print("loop");
      } else {
        drawArrow(40, 40, bearing, ST77XX_CYAN);  // map direction: hold the screen top to the north
        tft.setCursor(37, 6);
        tft.print("N");
      }
    } else {
      tft.setCursor(84, 16);
      tft.setTextColor(ST77XX_WHITE);
      tft.print("Wacht op");
      tft.setCursor(84, 26);
      tft.print("eigen GPS");
    }
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(84, 46);
    tft.printf("%lus geleden", age);
  }
  if (!p || !own) {
    tft.setTextSize(3);
    tft.setCursor(32, 30);
    tft.print("?");
    tft.setTextSize(1);
  }
  tft.setCursor(84, 70);
  tft.setTextColor(own ? ST77XX_GREEN : ST77XX_RED);
  if (own == 2) tft.print("GPS test");
  else tft.printf("GPS %s %lu", own ? "sat" : "zoekt", gps.satellites.isValid() ? gps.satellites.value() : 0);
}

void drawFlash() {
  bool on = (millis() / 400) % 2;
  tft.fillScreen(on ? ST77XX_YELLOW : ST77XX_BLACK);
  tft.setTextColor(on ? ST77XX_BLACK : ST77XX_YELLOW);
  tft.setTextSize(2);
  tft.setCursor(8, 20);
  tft.printf("%s zoekt", flashFrom);
  tft.setCursor(8, 44);
  tft.print("je!");
  tft.setTextSize(1);
}

void drawPreset() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(0, 0);
  tft.printf("Bericht %d/%d", preset + 1, NUM_PRESETS);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(0, 22);
  tft.print(PRESETS[preset]);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(0, 50);
  if (preset == presetSent) tft.print(presetState);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(0, 70);
  tft.print("kort: verder  lang: stuur");
}

void showStats();
void drawScreen() {
  if ((int32_t)(millis() - flashUntil) < 0) drawFlash();
  else if (screen == LOCATE) drawLocate();
  else if (screen == PRESET) drawPreset();
  else showStats();
}

// Queue a message from the board itself; the Mac app hears about it through PRESET.
void sendPreset() {
  const char *text = PRESETS[preset];
  for (auto &p : outbox) {
    if (!p.active) {
      presetN = PRESET_N0 + presetCount++ % 100000;
      presetSent = preset;
      p = {true, presetN, 0, millis(), text};
      presetState = "Versturen...";
      Serial.printf("PRESET %u %s\n", presetN, text);
      return;
    }
  }
  presetSent = preset;
  presetState = "Wachtrij vol";
}

void showStats() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(ST77XX_YELLOW);
  tft.printf("LoRa chat  ID %s\n", myId);
  tft.setTextColor(ST77XX_WHITE);
  tft.printf("Verzonden %lu  Ontv. %lu\n", sentCount, recvCount);
  tft.printf("RSSI %.0f  SNR %.1f\n", lastRssi, lastSnr);
  tft.setTextColor(ST77XX_CYAN);
  tft.printf("Van %s:\n", lastFrom);
  tft.setTextColor(ST77XX_WHITE);
  tft.print(lastText.substring(0, 26 * 6));
}

void transmit(String packet) {
  int state = radio.transmit(packet);
  if (state != RADIOLIB_ERR_NONE) Serial.printf("ERR radio tx %d\n", state);
  rxFlag = false;
  radio.startReceive();
}

// Retries arrive within ~8 s. The window stops a restarted Mac app that reuses numbers from being muted.
bool alreadySeen(const char *from, unsigned n) {
  for (auto &s : seen)
    if (s.n == n && strcmp(s.from, from) == 0 && millis() - s.at < 30000) return true;
  strcpy(seen[seenNext].from, from);
  seen[seenNext].n = n;
  seen[seenNext].at = millis();
  seenNext = (seenNext + 1) % 16;
  return false;
}

void handleRadio(const String &pkt) {
  float rssi = radio.getRSSI(), snr = radio.getSNR();
  char kind = pkt[0];
  char a[5] = "", b[5] = "";
  unsigned n = 0;
  int textAt = 0;

  if (kind == 'M' && sscanf(pkt.c_str(), "M %4s %u %n", a, &n, &textAt) == 2 && textAt > 0) {
    if (strcmp(a, myId) == 0) return;
    delay(30);  // let the sender switch back to receive
    transmit("A " + String(myId) + " " + a + " " + n);
    if (alreadySeen(a, n)) return;  // a retry whose ack got lost
    lastRssi = rssi;
    lastSnr = snr;
    strcpy(lastFrom, a);
    lastText = pkt.substring(textAt);
    for (unsigned i = 0; i < lastText.length(); i++)
      if ((uint8_t)lastText[i] < 0x20 || lastText[i] == 0x7F) lastText.setCharAt(i, ' ');  // keep one line per message on serial
    recvCount++;
    Serial.printf("MSG %s %u %.0f %.1f %s\n", a, n, rssi, snr, lastText.c_str());
    screen = CHAT;
    drawScreen();
  } else if (kind == 'A' && sscanf(pkt.c_str(), "A %4s %4s %u", a, b, &n) == 3 && strcmp(b, myId) == 0) {
    for (auto &p : outbox) {
      if (p.active && p.n == n) {
        p.active = false;
        lastRssi = rssi;
        lastSnr = snr;
        Serial.printf("DELIVERED %u %.0f %.1f\n", n, rssi, snr);
        if (n == presetN) presetState = "Afgeleverd";
        drawScreen();
      }
    }
  } else if (kind == 'R' && sscanf(pkt.c_str(), "R %4s %4s %u", a, b, &n) == 3 && strcmp(b, myId) == 0) {
    Serial.printf("READ %s %u\n", a, n);
  } else if (kind == 'P' && sscanf(pkt.c_str(), "P %4s %n", a, &textAt) == 1 && textAt > 0 && strcmp(a, myId) != 0) {
    char *end;
    double plat = strtod(pkt.c_str() + textAt, &end), plon = strtod(end, nullptr);
    if (plat < -90 || plat > 90 || plon < -180 || plon > 180) return;
    Peer *slot = nullptr;
    for (auto &p : peers)
      if (p.used && strcmp(p.id, a) == 0) slot = &p;
    for (auto &p : peers)
      if (!slot && !p.used) slot = &p;
    if (!slot) slot = &peers[0];
    *slot = {true, "", plat, plon, millis(), rssi, snr};
    strcpy(slot->id, a);
    nextReportAt = 0;  // report the new position right away
  } else if (kind == 'F' && sscanf(pkt.c_str(), "F %4s", a) == 1 && strcmp(a, myId) != 0) {
    strcpy(flashFrom, a);
    flashUntil = millis() + FLASH_MS;
    Serial.printf("FINDME %s\n", a);
  }
}

void sendFindMe() {
  transmit("F " + String(myId));
  Serial.printf("FINDSENT\n");
}

void handleSerial(const String &line) {
  char from[5] = "";
  unsigned n = 0;
  int textAt = 0;
  if (line == "INFO") {
    Serial.printf("ID %s\n", myId);
  } else if (sscanf(line.c_str(), "SEND %u %n", &n, &textAt) == 1 && textAt > 0) {
    String text = line.substring(textAt);
    if (text.length() > MAX_TEXT) {
      Serial.printf("ERR text too long %u\n", n);
      return;
    }
    for (auto &p : outbox) {
      if (!p.active) {
        p = {true, n, 0, millis(), text};
        return;
      }
    }
    Serial.printf("ERR outbox full %u\n", n);
  } else if (sscanf(line.c_str(), "READ %4s %u", from, &n) == 2) {
    transmit("R " + String(myId) + " " + from + " " + n);
  } else if (line == "FINDME") {
    sendFindMe();
  } else if (line == "SETPOS off") {
    simPos = false;
  } else if (line.startsWith("SETPOS ")) {
    char *end;
    double lat = strtod(line.c_str() + 7, &end), lon = strtod(end, nullptr);
    if (lat < -90 || lat > 90 || lon < -180 || lon > 180 || (lat == 0 && lon == 0)) {
      Serial.printf("ERR bad position\n");
      return;
    }
    simPos = true;
    simLat = lat;
    simLon = lon;
    nextPosAt = millis();
  } else if (line.length()) {
    Serial.printf("ERR unknown command\n");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  pinMode(VEXT_EN, OUTPUT);
  digitalWrite(VEXT_EN, HIGH);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  delay(50);
  tftSpi.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.initR(INITR_MINI160x80_PLUGIN);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  uint64_t mac = ESP.getEfuseMac();
  snprintf(myId, sizeof(myId), "%02X%02X", (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));

  loraSpi.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  int state = radio.begin(FREQ_MHZ, BW_KHZ, SF, CR, SYNC_WORD, TX_DBM, 8, 1.8);
  if (state == RADIOLIB_ERR_NONE) state = radio.setDio2AsRfSwitch(true);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("ERR radio init %d\n", state);
    tft.setTextColor(ST77XX_RED);
    tft.printf("Radio fout %d", state);
    while (true) delay(1000);
  }
  Serial.printf("# radio %.3f MHz SF%d BW%.0f %d dBm\n", FREQ_MHZ, SF, BW_KHZ, TX_DBM);

  pinMode(GNSS_RST, OUTPUT);
  digitalWrite(GNSS_RST, HIGH);
  gnssSerial.begin(115200, SERIAL_8N1, GNSS_RX, GNSS_TX);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.printf("ID %s\n", myId);

  radio.setDio1Action(onDio1);
  radio.startReceive();
  randomSeed(esp_random());
  drawScreen();
}

void serviceOutbox();

void serviceLocation() {
  while (gnssSerial.available()) gps.encode(gnssSerial.read());

  // USER button. Short press: next screen, through locate, chat and each preset message.
  // Long press: send the preset on screen, or elsewhere ask the other board to flash.
  bool down = digitalRead(BUTTON_PIN) == LOW;
  if (down && !buttonDownAt) buttonDownAt = millis() | 1;
  if (!down && buttonDownAt) {
    if (millis() - buttonDownAt < LONG_PRESS_MS) {
      if (screen == LOCATE) {
        screen = CHAT;
      } else if (screen == CHAT) {
        screen = PRESET;
        preset = 0;
      } else if (++preset == NUM_PRESETS) {
        screen = LOCATE;
      }
    } else if (screen == PRESET) {
      sendPreset();
    } else {
      sendFindMe();
    }
    buttonDownAt = 0;
    drawScreen();
  }

  bool flashing = (int32_t)(millis() - flashUntil) < 0;
  digitalWrite(LED_PIN, flashing && (millis() / 150) % 2);
  static bool wasFlashing = false;
  static uint32_t lastFlashDraw = 0;
  if (flashing && millis() - lastFlashDraw > 400) {
    lastFlashDraw = millis();
    drawFlash();
  }
  if (wasFlashing && !flashing) drawScreen();
  wasFlashing = flashing;

  double lat, lon;
  int own = ownPos(lat, lon);
  if (own && (int32_t)(millis() - nextPosAt) >= 0) {
    transmit("P " + String(myId) + " " + String(lat, 6) + " " + String(lon, 6));
    nextPosAt = millis() + POS_EVERY_MS + random(0, 3000);
  }

  if ((int32_t)(millis() - nextReportAt) < 0) return;
  nextReportAt = millis() + REPORT_EVERY_MS;
  Serial.printf("GPS %d %lu %.6f %.6f %.1f %.0f %.1f\n", own, gps.satellites.isValid() ? gps.satellites.value() : 0,
                own ? lat : 0, own ? lon : 0, own == 1 ? gps.speed.kmph() : 0, own == 1 ? gps.course.deg() : 0,
                gps.hdop.isValid() ? gps.hdop.hdop() : 0);
  for (auto &p : peers) {
    if (!p.used) continue;
    double dist = own ? TinyGPSPlus::distanceBetween(lat, lon, p.lat, p.lon) : -1;
    double bearing = own ? TinyGPSPlus::courseTo(lat, lon, p.lat, p.lon) : -1;
    Serial.printf("PEER %s %.6f %.6f %lu %.0f %.0f %.0f %.1f\n", p.id, p.lat, p.lon, (millis() - p.at) / 1000, dist,
                  bearing, p.rssi, p.snr);
  }
  if (!flashing) drawScreen();
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      serialLine.trim();
      handleSerial(serialLine);
      serialLine = "";
    } else if (serialLine.length() < MAX_TEXT + 32) {
      serialLine += c;
    }
  }

  if (rxFlag) {
    rxFlag = false;
    String pkt;
    int state = radio.readData(pkt);
    radio.startReceive();
    if (state == RADIOLIB_ERR_NONE) handleRadio(pkt);
    else if (state != RADIOLIB_ERR_CRC_MISMATCH) Serial.printf("ERR radio rx %d\n", state);
  }

  serviceLocation();
  serviceOutbox();
}

void serviceOutbox() {
  // One message in flight at a time, oldest first, so messages arrive in order.
  Pending *next = nullptr;
  for (auto &p : outbox) {
    if (!p.active) continue;
    if (p.tries > 0) { next = &p; break; }
    if (!next || (int32_t)(p.nextAt - next->nextAt) < 0) next = &p;
  }
  if (!next || (int32_t)(millis() - next->nextAt) < 0) return;
  if (next->tries == TRIES) {
    next->active = false;
    Serial.printf("FAILED %u\n", next->n);
    if (next->n == presetN) {
      presetState = "Niet afgeleverd";
      drawScreen();
    }
    return;
  }
  transmit("M " + String(myId) + " " + next->n + " " + next->text);
  if (next->tries++ == 0) {
    Serial.printf("SENT %u\n", next->n);
    sentCount++;
    drawScreen();
  }
  next->nextAt = millis() + ACK_WAIT_MS + random(0, 500);
}
