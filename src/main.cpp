/**
 * BLE_Send – Heltec WiFi LoRa 32 V3
 *
 * Flow every SCAN_INTERVAL_MS:
 *   1. BLE active scan  →  collect MAC + RSSI
 *   2. Pack into compact JSON
 *   3. LoRa TX  →  wait ACK_WAIT_MS for ACK from receiver
 *   4. Update OLED with live status
 *
 * P2P Link health:
 *   ONLINE  – ACK received within ACK_WAIT_MS after TX
 *   NO ACK  – no reply within timeout (receiver may be out of range / busy)
 *
 * OLED layout (SSD1306 128×64):
 *   ╔══ BLE Sender ══╗
 *   ║ LoRa: TX OK    ║
 *   ║ BLE : 3 found  ║
 *   ║ Link: ONLINE   ║
 *   ║ Next: 12s      ║
 *   ╚════════════════╝
 *
 * LoRa pins (Heltec V3): NSS=8 DIO1=14 RST=12 BUSY=13
 * SPI pins             : SCK=9 MISO=11 MOSI=10
 * OLED pins            : RST=21 SCL=18 SDA=17
 */

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <RadioLib.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <ArduinoJson.h>
#include <vector>

#include "config.h"

// ═══════════════════════════════════════════════
//  Hardware objects
// ═══════════════════════════════════════════════
SX1262 radio = new Module(SX_NSS, SX_DIO1, SX_RST, SX_BUSY);
U8G2_SSD1306_128X64_NONAME_F_SW_I2C display(U8G2_R0, OLED_SCL, OLED_SDA, OLED_RST);

// ─── ACK RX interrupt flag ────────────────────
volatile bool ackFlag = false;
IRAM_ATTR void onAckDone() { ackFlag = true; }

// ─── BLE ──────────────────────────────────────
// MAC stored as 6 raw bytes (MSB first, same order as string representation)
struct BLEEntry { uint8_t mac[6]; int8_t rssi; };
static std::vector<BLEEntry> devList;  // grows dynamically – no hard cap

class ScanCB : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice d) override {
        // Guard: keep at least 8 KB heap free for BT stack and LoRa operations
        if (ESP.getFreeHeap() < 8192) return;
        BLEEntry e;
        // getNative() returns bytes LSB-first; reverse to MSB-first for packet
        const uint8_t* native = reinterpret_cast<const uint8_t*>(d.getAddress().getNative());
        for (int j = 0; j < 6; j++) e.mac[j] = native[5 - j];
        e.rssi = static_cast<int8_t>(d.getRSSI());
        devList.push_back(e);
    }
};
static ScanCB   scanCB;
static BLEScan *bleScan = nullptr;

// ─── Display state ────────────────────────────
static char     sLoRa[20]   = "Init";
static char     sBLE[20]    = "---";
static char     sLink[12]   = "---";
static char     sRSSI[20]   = "---";   // RSSI of last ACK from receiver
static unsigned long nextScanAt = 0;

// ═══════════════════════════════════════════════
//  OLED
// ═══════════════════════════════════════════════
static void initOledPowerAndReset() {
    // OLED power is supplied by Vext on this board, LOW means ON.
    pinMode(VEXT_CTRL, OUTPUT);
    digitalWrite(VEXT_CTRL, LOW);
    delay(30);

    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, LOW);
    delay(20);
    digitalWrite(OLED_RST, HIGH);
    delay(20);
}

void oledUpdate() {
    char tmp[32];
    display.clearBuffer();
    display.setFont(u8g2_font_6x10_tf);

    // Title
    display.drawStr(0, 10, "=== BLE Sender ===");
    display.drawHLine(0, 12, 128);

    // LoRa
    snprintf(tmp, sizeof(tmp), "LoRa: %s", sLoRa);
    display.drawStr(0, 24, tmp);

    // BLE
    snprintf(tmp, sizeof(tmp), "BLE : %s", sBLE);
    display.drawStr(0, 36, tmp);

    // Link health
    snprintf(tmp, sizeof(tmp), "Link: %s", sLink);
    display.drawStr(0, 48, tmp);

    // RSSI of last ACK from receiver
    snprintf(tmp, sizeof(tmp), "RSSI: %s", sRSSI);
    display.drawStr(0, 62, tmp);

    display.sendBuffer();
}

// ═══════════════════════════════════════════════
//  Initialisation
// ═══════════════════════════════════════════════
void initOLED() {
    initOledPowerAndReset();
    display.begin();
    display.setContrast(255);
    display.clearBuffer();
    display.setFont(u8g2_font_6x10_tf);
    display.drawStr(0, 10, "=== BLE Sender ===");
    display.drawStr(0, 30, "Booting...");
    display.sendBuffer();
    delay(800);
}

void initLoRa() {
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SX_NSS);
    int16_t st = radio.begin(LORA_FREQUENCY, LORA_BW, LORA_SF, LORA_CR,
                              RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                              LORA_TX_POWER, LORA_PREAMBLE);
    if (st != RADIOLIB_ERR_NONE) {
        snprintf(sLoRa, sizeof(sLoRa), "ERR %d", st);
        oledUpdate();
        Serial.printf("[LoRa] Init failed (err %d) – halting\n", st);
        while (true) delay(1000);
    }
    radio.setDio2AsRfSwitch(true);
    strncpy(sLoRa, "Ready", sizeof(sLoRa));
    Serial.println("[LoRa] Ready");
}

void initBLE() {
    BLEDevice::init("");
    bleScan = BLEDevice::getScan();
    bleScan->setAdvertisedDeviceCallbacks(&scanCB, false);
    bleScan->setActiveScan(true);
    bleScan->setInterval(100);
    bleScan->setWindow(99);
    Serial.println("[BLE] Ready");
}

// ═══════════════════════════════════════════════
//  BLE scan
// ═══════════════════════════════════════════════
void doScan() {
    devList.clear();
    strncpy(sBLE, "Scanning...", sizeof(sBLE));
    oledUpdate();
    Serial.println("[BLE] Scanning...");
    bleScan->start(BLE_SCAN_SEC, false);
    bleScan->clearResults();
    snprintf(sBLE, sizeof(sBLE), "%d found", (int)devList.size());
    Serial.printf("[BLE] %d device(s) found\n", (int)devList.size());
    oledUpdate();
}

// ═══════════════════════════════════════════════
//  Wait for ACK from receiver (polling RX)
//  Returns true if a valid ACK packet arrived within ACK_WAIT_MS
// ═══════════════════════════════════════════════
bool waitAck() {
    // radio.available() only works when a DIO1 interrupt handler is set.
    // We attach one temporarily here, then clear it after the wait.
    ackFlag = false;
    radio.setDio1Action(onAckDone);
    radio.startReceive();

    unsigned long deadline = millis() + ACK_WAIT_MS;
    bool found = false;

    while (millis() < deadline) {
        if (ackFlag) {
            ackFlag = false;
            String ackStr;
            int16_t rdSt = radio.readData(ackStr);
            if (rdSt == RADIOLIB_ERR_NONE) {
                Serial.printf("[ACK] RX: %s\n", ackStr.c_str());
                StaticJsonDocument<128> ack;
                if (!deserializeJson(ack, ackStr) && ack["ack"] == 1) {
                    float rssi = radio.getRSSI();
                    snprintf(sRSSI, sizeof(sRSSI), "%.0fdBm", rssi);
                    found = true;
                    break;
                }
            } else {
                Serial.printf("[ACK] Read failed (err %d)\n", rdSt);
            }
            // Not a valid ACK — keep listening
            radio.startReceive();
        }
        delay(1);
    }

    radio.clearDio1Action();
    return found;
}

// ═══════════════════════════════════════════════
//  Build binary multi-packet burst → LoRa TX → ACK per packet
//  Binary format: 10-byte header + 7 bytes/device (6 MAC + 1 RSSI)
//  35 devices/packet; total packets capped at 255 (uint8) → ~8,925 devices
// ═══════════════════════════════════════════════
void buildAndSend(int reportedTotal = -1) {
    if (devList.empty()) return;

    int      total      = (int)devList.size();
    int      hdrTotal   = (reportedTotal < 0) ? total : reportedTotal;  // value written to packet header
    int      totalPkt   = (total + DEVS_PER_PKT - 1) / DEVS_PER_PKT;
    if (totalPkt > 255) totalPkt = 255;  // uint8_t field limit
    int      okPkt    = 0;
    uint8_t  idLen    = (uint8_t)strlen(NODE_ID);
    if (idLen > 3) idLen = 3;

    Serial.printf("[TX] Burst: %d devs → %d packets\n", total, totalPkt);

    for (int pkt = 0; pkt < totalPkt; pkt++) {
        int startI = pkt * DEVS_PER_PKT;
        int endI   = startI + DEVS_PER_PKT;
        if (endI > total) endI = total;
        int cnt    = endI - startI;

        uint8_t buf[LORA_MAX_PAYLOAD];
        buf[0]  = 0xBE;                  // magic
        buf[1]  = (uint8_t)pkt;
        buf[2]  = (uint8_t)totalPkt;
        buf[3]  = idLen;
        memset(&buf[4], 0, 3);
        memcpy(&buf[4], NODE_ID, idLen);
        buf[7]  = (uint8_t)cnt;
        buf[8]  = (uint8_t)(hdrTotal & 0xFF);         // total devices LSB
        buf[9]  = (uint8_t)((hdrTotal >> 8) & 0xFF);  // total devices MSB

        int pos = BIN_HEADER_LEN;
        for (int i = startI; i < endI; i++) {
            memcpy(&buf[pos], devList[i].mac, 6);          // already binary MSB-first
            buf[pos + 6] = (uint8_t)(int8_t)devList[i].rssi;
            pos += BYTES_PER_DEV;
        }

        snprintf(sLoRa, sizeof(sLoRa), "TX %d/%d", pkt + 1, totalPkt);
        oledUpdate();
        Serial.printf("[TX] Pkt %d/%d – %d devs, %d bytes\n",
                      pkt + 1, totalPkt, cnt, pos);

        int16_t st = radio.transmit(buf, (size_t)pos);
        if (st != RADIOLIB_ERR_NONE) {
            Serial.printf("[TX] Pkt %d failed (err %d)\n", pkt + 1, st);
            continue;
        }

        if (waitAck()) {
            okPkt++;
        } else {
            Serial.printf("[ACK] Pkt %d: no ACK\n", pkt + 1);
        }
        delay(20);  // brief inter-packet gap
    }

    if (okPkt == totalPkt) {
        strncpy(sLink, "ONLINE", sizeof(sLink));
        snprintf(sLoRa, sizeof(sLoRa), "OK %d pkts", totalPkt);
    } else if (okPkt > 0) {
        strncpy(sLink, "PARTIAL", sizeof(sLink));
        snprintf(sLoRa, sizeof(sLoRa), "%d/%d pkts", okPkt, totalPkt);
    } else {
        strncpy(sLink, "NO ACK", sizeof(sLink));
        strncpy(sLoRa, "No ACK", sizeof(sLoRa));
    }
    Serial.printf("[TX] Burst done: %d/%d packets OK\n", okPkt, totalPkt);
    oledUpdate();
}

// ═══════════════════════════════════════════════
//  Arduino entry points
// ═══════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=== LoRa P2P – BLE Sender ===");

    initOLED();
    initLoRa();
    initBLE();

    nextScanAt = millis();
    oledUpdate();
}

void loop() {
    if (millis() >= nextScanAt) {
        doScan();
        if (!devList.empty()) {
            buildAndSend();
        } else {
            // No BLE devices found – send own MAC as heartbeat (RSSI=0, no=0/0)
            BLEEntry self;
            const uint8_t* native = reinterpret_cast<const uint8_t*>(
                BLEDevice::getAddress().getNative());
            for (int j = 0; j < 6; j++) self.mac[j] = native[5 - j];
            self.rssi = 0;
            devList.push_back(self);
            Serial.println("[INFO] No BLE devices – sending own MAC as heartbeat");
            buildAndSend(0);  // reportedTotal=0 → receiver sees no=0/0
            strncpy(sLoRa, "Heartbeat", sizeof(sLoRa));
            oledUpdate();
        }
        // Start interval AFTER work finishes, not before
        nextScanAt = millis() + SCAN_INTERVAL_MS;
    }

    // Refresh countdown every second
    static unsigned long lastRefresh = 0;
    if (millis() - lastRefresh >= 1000UL) {
        lastRefresh = millis();
        oledUpdate();
    }
}
