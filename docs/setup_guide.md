# AI based Air Quality Monitoring using ESP32 - Setup Guide

Detailed setup guide for hardware wiring, Sensirion SPS30 configuration, Indian CPCB AQI calculation logic, CircuitDigest Cloud setup, and WhatsApp API integration.

---

## 1. Hardware Connections & Pinout

### Component List
- **ESP32 Development Board** (30-pin or 38-pin version)
- **Sensirion SPS30 Optical PM Sensor** (UART mode)
- **2.4" ILI9341 SPI TFT Display (240x320 resolution)**
- **5V 2A Power Adapter / USB Power Source**

### Wiring Table

| Sensirion SPS30 Pin | ESP32 Pin | Notes |
| :--- | :--- | :--- |
| `Pin 1 (VDD)` | `5V` | Must be powered with 5V for internal fan motor |
| `Pin 2 (RX)` | `GPIO 17` (TX2) | Serial UART TX to RX |
| `Pin 3 (TX)` | `GPIO 16` (RX2) | Serial UART RX to TX |
| `Pin 4 (Select)` | `GND` | Pull LOW for UART interface selection |
| `Pin 5 (GND)` | `GND` | Common Ground |

| ILI9341 TFT Pin | ESP32 Pin | Notes |
| :--- | :--- | :--- |
| `VCC` | `3.3V` / `5V` | Power supply |
| `GND` | `GND` | Common Ground |
| `CS` | `GPIO 2` | Chip Select |
| `RESET` | `GPIO 4` | Display Reset |
| `DC` | `GPIO 5` | Data / Command Select |
| `SDI (MOSI)` | `GPIO 23` | SPI MOSI |
| `SCK (CLK)` | `GPIO 18` | SPI Clock |
| `LED` | `3.3V` | Backlight Power |

### Wiring Diagram
![Wiring Diagram](images/Wiring-Diagram.jpg)

---

## 2. Indian CPCB AQI Breakpoint Reference

The system calculates sub-indices for $\text{PM}_{2.5}$ and $\text{PM}_{10}$ using standard CPCB formula interpolation:

$$\text{Sub-Index} = I_{\text{low}} + \frac{I_{\text{high}} - I_{\text{low}}}{C_{\text{high}} - C_{\text{low}}} \times (C - C_{\text{low}})$$

The final CPCB AQI is defined as:
$$\text{CPCB AQI} = \max(\text{Sub-Index}_{\text{PM2.5}}, \text{Sub-Index}_{\text{PM10}})$$

| Category | CPCB AQI Range | PM2.5 Range ($\mu\text{g/m}^3$) | PM10 Range ($\mu\text{g/m}^3$) |
| :--- | :---: | :---: | :---: |
| **Good** | 0 – 50 | 0 – 30 | 0 – 50 |
| **Satisfactory** | 51 – 100 | 31 – 60 | 51 – 100 |
| **Moderate** | 101 – 200 | 61 – 90 | 101 – 250 |
| **Poor** | 201 – 300 | 91 – 120 | 251 – 350 |
| **Very Poor** | 301 – 400 | 121 – 250 | 351 – 430 |
| **Severe** | 401 – 500 | 250+ | 430+ |

---

## 3. CircuitDigest Cloud Configuration

1. Log into your [CircuitDigest Cloud](https://www.circuitdigest.cloud) dashboard.
2. Create a new device under **Devices** -> **Add New Device**.
3. Configure 10 data keys (`analog-input-1` to `analog-input-10`).
4. Update the sketch ([`code/airqualitymonitor/airqualitymonitor.ino`](../code/airqualitymonitor/airqualitymonitor.ino)) credentials:

```cpp
#define WIFI_SSID      "your-Wi-Fi SSID"
#define WIFI_PASS      "your-Wi-Fi password"
#define DEVICE_ID      "your-device-id"
#define CONNECTION_KEY "your-connection-key"
#define API_KEY        "your-api-key"
#define WHATSAPP_PHONE "your-registered-phone-number"
```
