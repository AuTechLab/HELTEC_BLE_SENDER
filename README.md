# BLE_Send – Heltec WiFi LoRa 32 V3

ฝั่ง **Sender** ของระบบ P2P LoRa สำหรับส่งข้อมูล BLE scan ไปยัง Receiver คู่สื่อสาร  
(คู่กับโปรเจกต์ `BLE_Receiver` ในโฟลเดอร์เดียวกัน)

---

## ภาพรวมการทำงาน

```
[ทุก SCAN_INTERVAL_MS = 60 วินาที]
  │
  ▼
BLE Active Scan (30 วินาที)
  │  เก็บ MAC + RSSI ของอุปกรณ์ที่พบ (สูงสุด 300 เครื่อง)
  ▼
แบ่งข้อมูลเป็น Packet ขนาด 35 เครื่อง/Packet (Binary format)
  │
  ▼
LoRa TX ทีละ Packet → รอ ACK ภายใน 3,000 ms
  │  ถ้าได้ ACK → ส่ง Packet ถัดไป
  │  ถ้าไม่ได้ ACK → บันทึก NO ACK แล้วข้ามไป
  ▼
อัปเดต OLED แสดงสถานะ
```

---

## Hardware

| ส่วนประกอบ | รุ่น |
|------------|------|
| Microcontroller | Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262) |
| OLED | SSD1306 128×64 (I2C built-in) |
| LoRa module | SX1262 (built-in) |

### Pin Mapping

| ฟังก์ชัน | GPIO |
|---------|------|
| SX1262 NSS | 8 |
| SX1262 DIO1 | 14 |
| SX1262 RST | 12 |
| SX1262 BUSY | 13 |
| SPI SCK | 9 |
| SPI MISO | 11 |
| SPI MOSI | 10 |
| OLED RST | 21 |
| OLED SCL | 18 |
| OLED SDA | 17 |
| Vext Control (OLED power) | 36 (OUTPUT, LOW = เปิด) |

---

## การตั้งค่า (User Configuration)

แก้ไขค่าใน `include/config.h`:

| ค่าคงที่ | ค่าเริ่มต้น | คำอธิบาย |
|---------|-----------|---------|
| `NODE_ID` | `"S01"` | ชื่อ Node ของ Sender (ไม่เกิน 3 ตัวอักษร) |
| `LORA_FREQUENCY` | `923.0` MHz | ความถี่ LoRa (AS923 สำหรับไทย/SEA) ต้องตรงกับ Receiver |
| `SCAN_INTERVAL_MS` | `60000` ms | ระยะเวลาระหว่างรอบ scan-and-send |
| `LORA_BW` | `125.0` kHz | Bandwidth (ต้องตรงกับ Receiver) |
| `LORA_SF` | `7` | Spreading Factor (ต้องตรงกับ Receiver) |
| `LORA_CR` | `5` | Coding Rate (ต้องตรงกับ Receiver) |
| `LORA_TX_POWER` | `22` dBm | กำลังส่ง LoRa |
| `BLE_SCAN_SEC` | `30` s | ระยะเวลา BLE scan ต่อรอบ |
| `BLE_MAX_DEVS` | `300` | จำนวน BLE device สูงสุดที่เก็บได้ต่อรอบ |
| `ACK_WAIT_MS` | `3000` ms | Timeout รอรับ ACK หลังจาก TX แต่ละ Packet |

---

## รูปแบบ Packet (Binary Protocol)

ข้อมูลถูกส่งเป็น Binary เพื่อประหยัด Airtime

```
Byte 0      : 0xBE  (Magic byte – ระบุประเภท packet)
Byte 1      : Packet index (0-based)
Byte 2      : Total packets ในชุดนี้
Byte 3      : ความยาว NODE_ID (≤ 3)
Bytes 4–6   : NODE_ID (zero-padded ถึง 3 bytes)
Byte 7      : จำนวน BLE device ใน packet นี้
Bytes 8–9   : จำนวน BLE device ทั้งหมดในรอบ scan นี้ (uint16_t, little-endian)
               └─ Receiver ใช้ค่านี้สร้าง "no": "<ลำดับ>/<ทั้งหมด>" ต่อ device ใน MQTT
Bytes 10+   : ข้อมูล device ทีละ 7 bytes
               └─ 6 bytes MAC (binary)
               └─ 1 byte RSSI (int8_t)
```

- Payload สูงสุด: **255 bytes**
- BLE device ต่อ Packet: **35 เครื่อง** (floor((255 − 10) / 7))
- 300 เครื่อง → ส่งสูงสุด **9 Packet** ต่อรอบ

---

## สถานะการเชื่อมต่อ (Link Health)

| สถานะ | ความหมาย |
|-------|---------|
| `ONLINE` | ได้รับ ACK ครบทุก Packet |
| `PARTIAL` | ได้รับ ACK บางส่วน |
| `NO ACK` | ไม่ได้รับ ACK เลย (Receiver อาจอยู่นอกพิสัย) |

---

## OLED Layout

```
=== BLE Sender ===
──────────────────
LoRa: TX OK
BLE : 42 found
Link: ONLINE
RSSI: -85dBm
```

> บรรทัดสุดท้าย `RSSI` คือ RSSI ของ ACK ที่ได้รับจาก Receiver

---

## Dependencies (PlatformIO)

| Library | Version |
|---------|---------|
| `jgromes/RadioLib` | ^6.6.0 |
| `bblanchon/ArduinoJson` | ^6.21.5 |
| `olikraus/U8g2` | ^2.35.7 |

---

## การ Build และ Upload

```bash
# Build
pio run

# Upload
pio run --target upload

# Serial Monitor
pio device monitor --baud 115200
```

> **หมายเหตุ:** หาก Upload ไม่ได้ ให้ตรวจสอบว่า Serial Monitor หรือโปรแกรมอื่นไม่ได้ใช้งาน COM port อยู่

---

## โครงสร้างโปรเจกต์

```
BLE_Send/
├── src/
│   └── main.cpp          # โค้ดหลัก
├── include/
│   └── config.h          # User configuration (NODE_ID, LoRa params, BLE params)
├── lib/                  # Library เพิ่มเติม (ว่าง)
├── test/                 # Unit test (ว่าง)
├── platformio.ini        # PlatformIO config
└── README.md             # ไฟล์นี้
```

---

## โปรเจกต์คู่ (Receiver)

ดู `../BLE_Receiver/` สำหรับฝั่งรับข้อมูล ซึ่งทำหน้าที่:
- รับ Packet Binary จาก BLE_Send
- ส่ง ACK ตอบกลับ
- แสดงข้อมูล BLE device ที่ได้รับ
