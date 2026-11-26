// LoRa chat between Heltec Wireless Tracker V1.1/V1.2 boards.
// The Mac app (firmware/chat) talks to the board over USB serial; the board relays over LoRa.
// Pins from Meshtastic variants/esp32s3/heltec_wireless_tracker/variant.h.
//
// Serial, host to board:  INFO | SEND <n> <text> | READ <from> <n>
// Serial, board to host:  ID <id> | SENT <n> | DELIVERED <n> <rssi> <snr> | FAILED <n>
//                         MSG <from> <n> <rssi> <snr> <text> | READ <from> <n> | ERR <text>
// Radio packets:          M <from> <n> <text> | A <acker> <origFrom> <n> | R <reader> <origFrom> <n>
#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <Adafruit_ST7735.h>

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
#define VEXT_EN 3  // active HIGH on V1.1/V1.2

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

SPIClass loraSpi(FSPI);
SPIClass tftSpi(HSPI);
SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSpi);
Adafruit_ST7735 tft(&tftSpi, TFT_CS, TFT_DC, TFT_RST);

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
    recvCount++;
    Serial.printf("MSG %s %u %.0f %.1f %s\n", a, n, rssi, snr, lastText.c_str());
    showStats();
  } else if (kind == 'A' && sscanf(pkt.c_str(), "A %4s %4s %u", a, b, &n) == 3 && strcmp(b, myId) == 0) {
    for (auto &p : outbox) {
      if (p.active && p.n == n) {
        p.active = false;
        lastRssi = rssi;
        lastSnr = snr;
        Serial.printf("DELIVERED %u %.0f %.1f\n", n, rssi, snr);
        showStats();
      }
    }
  } else if (kind == 'R' && sscanf(pkt.c_str(), "R %4s %4s %u", a, b, &n) == 3 && strcmp(b, myId) == 0) {
    Serial.printf("READ %s %u\n", a, n);
  }
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
  Serial.printf("ID %s\n", myId);

  radio.setDio1Action(onDio1);
  radio.startReceive();
  randomSeed(esp_random());
  showStats();
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
    return;
  }
  transmit("M " + String(myId) + " " + next->n + " " + next->text);
  if (next->tries++ == 0) {
    Serial.printf("SENT %u\n", next->n);
    sentCount++;
    showStats();
  }
  next->nextAt = millis() + ACK_WAIT_MS + random(0, 500);
}
