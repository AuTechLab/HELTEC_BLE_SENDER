#pragma once

// ═══════════════════════════════════════════════
//  User Configuration  ← Edit these
// ═══════════════════════════════════════════════
#define NODE_ID          "S01"
#define LORA_FREQUENCY   923.0f   // MHz (AS923 TH/SEA). Must match receiver.
#define SCAN_INTERVAL_MS 60000UL  // ms between scan-and-send cycles (1 min: allows multi-pkt TX + duty cycle)

// ─── LoRa parameters (must match receiver) ────
#define LORA_BW        125.0f
#define LORA_SF        7
#define LORA_CR        5
#define LORA_TX_POWER  14
#define LORA_PREAMBLE  8

// ─── Heltec V3 SX1262 pins ────────────────────
#define SX_NSS   8
#define SX_DIO1  14
#define SX_RST   12
#define SX_BUSY  13
#define SPI_SCK  9
#define SPI_MISO 11
#define SPI_MOSI 10

// ─── Heltec V3 OLED (SSD1306 128×64 I2C) ─────
#define VEXT_CTRL 36
#define OLED_RST  21
#define OLED_SCL  18
#define OLED_SDA  17

// ─── BLE ──────────────────────────────────────
#define BLE_SCAN_SEC  30    // active scan duration (seconds)
#define BLE_MAX_DEVS  300   // maximum BLE devices collected per cycle

// ─── ACK ──────────────────────────────────────
#define ACK_WAIT_MS  3000   // ms to wait for ACK reply after TX

// ─── Binary packet format ─────────────────────
// Byte  0    : 0xBE  (magic – identifies binary BLE packet)
// Byte  1    : packet index (0-based, uint8)
// Byte  2    : total packets in this burst (uint8)
// Byte  3    : NODE_ID length (≤3)
// Bytes 4-6  : NODE_ID zero-padded to 3 bytes
// Byte  7    : device count in this packet (uint8)
// Bytes 8+   : devices – 6-byte MAC (binary) + 1-byte RSSI (int8_t)
#define LORA_MAX_PAYLOAD 255
#define BIN_HEADER_LEN   8
#define BYTES_PER_DEV    7    // 6 MAC + 1 RSSI
#define DEVS_PER_PKT     ((LORA_MAX_PAYLOAD - BIN_HEADER_LEN) / BYTES_PER_DEV)  // 35
