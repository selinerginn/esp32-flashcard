# ESP32 Digital Flashcard System

A portable, battery-powered flashcard study device built with ESP32. Create and manage card decks through a web interface, sync over Wi-Fi, and study completely offline.

![ESP32 Flashcard Device](docs/device-front.jpg)
![ESP32 Flashcard Device](docs/device-side.jpg)
---

## Features

- **Offline-first** — sync once over Wi-Fi, study anywhere without internet
- **Web management interface** — create, edit, delete decks and cards from your browser
- **CSV import** — bulk import cards via drag & drop
- **Score-based card selection** — harder cards appear more frequently (weighted algorithm)
- **Deep sleep** — long press to power off, long press to wake up; saves battery
- **Multi-network Wi-Fi** — supports multiple saved networks including WPA2-Enterprise (PEAP/MSCHAPv2)
- **Filter modes** — study ALL cards, only HARD (score ≤ 0), or NEW (never seen)
- **Shuffle mode** — toggle between weighted and random card selection
- **Persistent scores** — scores survive power cycles and deck updates

---

## Hardware

| Component | Details |
|---|---|
| ESP32 Dev Board | Dual-core 240 MHz, 520 KB SRAM, 4 MB Flash |
| SSD1306 OLED | 128×64 px, I2C (address 0x3C) |
| 18650 Battery Shield V3 | 5 V / 2 A output, USB-C charging |
| Push Buttons ×3 | Connected to GPIO 13, 27, 26 |

### Wiring

| ESP32 Pin | Connected To |
|---|---|
| VIN | Battery Shield 5V (soldered) |
| GND | Battery Shield GND (soldered) |
| GPIO 21 (SDA) | OLED SDA (jumper) |
| GPIO 22 (SCL) | OLED SCL (jumper) |
| GPIO 13 | Button — Menu (jumper) |
| GPIO 27 | Button — OK (jumper) |
| GPIO 26 | Button — Down (jumper) |

Buttons are wired between GPIO pin and GND. Internal pull-up resistors are enabled in firmware — no external resistors needed.

---

## Button Reference

| Button | Short Press | Long Press (≥1s) |
|---|---|---|
| GPIO 13 (Menu) | Open / close menu | Power off |
| GPIO 27 (OK) | Show answer / Confirm | — |
| GPIO 26 (Down) | Mark unknown / Navigate down | — |

---

## Menu Structure

```
Decks       → select active deck (no Wi-Fi needed)
Sync        → connect Wi-Fi, download all decks, disconnect
Stats       → total / known / learning / unseen / hard
Settings  
  Shuffle   → ON / OFF
  Filter    → ALL / HARD / NEW
  Brightness→ LOW / MED / HIGH
Reset Scores→ resets scores for active deck
```

---

## Software Requirements

### ESP32 (Arduino IDE)

Install the following libraries via **Library Manager**:

- `Adafruit SSD1306`
- `Adafruit GFX Library`
- `ArduinoJson` (by Benoit Blanchon)

Board: **ESP32 Dev Module** (install via Boards Manager: `esp32` by Espressif)

### Server (Python)

```bash
pip install flask
```

---

## Setup

### 1. Configure Wi-Fi and server address

Copy `config.example.h` to `config.h` inside the `flashcard/` folder:

```bash
cp flashcard/config.example.h flashcard/config.h
```

Edit `config.h` with your own values:

```cpp
#define SERVER_URL "http://192.168.x.x:5000"  // your PC's IP address

WiFiNetwork networks[] = {
  { "YOUR_WIFI_NAME", "YOUR_WIFI_PASSWORD", NULL },
  // WPA2-Enterprise (e.g. university network):
  // { "NETWORK_NAME", "YOUR_PASSWORD", "YOUR_USERNAME" },
};
const int NETWORK_COUNT = 1;
```

> ⚠️ `config.h` is listed in `.gitignore` and will NOT be committed. Never share this file — it contains your Wi-Fi credentials.

### 2. Upload firmware

1. Open `flashcard/flashcard.ino` in Arduino IDE
2. Select your board: **Tools → Board → ESP32 Dev Module**
3. Select the correct port: **Tools → Port**
4. Click **Upload**

### 3. Start the server

```bash
cd server
python server.py
```

The server will print its address:
```
* Running on http://192.168.x.x:5000
```

Use this IP address in your `config.h` as `SERVER_URL`.

### 4. Open the web interface

Go to `http://192.168.x.x:5000` in your browser.

- Create a deck from the left sidebar
- Add cards using the form, or import a CSV file
- CSV format: `question,answer` (one card per line, header row optional)

### 5. Sync the device

On the device, go to **Menu → Sync**. The device will:
1. Connect to the first available saved Wi-Fi network
2. Download all decks and cards in a single request
3. Save everything to local flash memory
4. Disconnect Wi-Fi

After syncing, the device works fully offline.

---

## NVS Storage Limits

Card data is stored in ESP32's NVS (Non-Volatile Storage) flash:

**Device (firmware):**
- Maximum **5 decks**
- Maximum **30 cards per deck** (hard limit set in firmware; physical NVS limit is ~54)

To change the firmware limits, edit in `flashcard.ino`:
```cpp
#define MAX_DECKS  5
#define MAX_CARDS  30
```

**Server:**
- Maximum **10 decks**
- Maximum **50 cards per deck**
- Maximum **50 cards total across all decks**

To change the server limits, edit in `server.py`:
```python
MAX_DECKS = 10
MAX_CARDS = 50
TOTAL_MAX_CARDS = 50
```

---

## How Card Selection Works

**Shuffle OFF (default):** Weighted random selection. Each card's probability is proportional to `(maxScore + 1) - ownScore`. Cards with lower scores appear more often. The same card never appears twice in a row.

**Shuffle ON:** Uniform random selection from the filtered set.

**Score system:**
| Score | Category |
|---|---|
| ≥ 3 | Known |
| 1 – 2 | Learning |
| 0 | Unseen |
| < 0 | Hard |

---

## Project Structure

```
esp32-flashcard/
├── flashcard/
│   ├── flashcard.ino       # ESP32 firmware
│   └── config.example.h   # Wi-Fi / server config template
├── server/
│   ├── server.py           # Flask REST API + web UI server
│   ├── templates/
│   │   └── index.html      # Web management interface
│   └── static/
│       ├── style.css
│       └── script.js
├── .gitignore
└── README.md
```

---

## Tech Stack

| Layer | Technology |
|---|---|
| Microcontroller | ESP32 (Arduino framework) |
| Display | Adafruit SSD1306 + GFX |
| JSON | ArduinoJson |
| Storage | ESP32 NVS (Preferences library) |
| Server | Python + Flask |
| Data | JSON file (decks.json) |
| Web UI | Vanilla HTML/CSS/JS (single file) |

---

## Troubleshooting

**Sync fails with HTTP error:**
- Make sure `server.py` is running
- Check that `SERVER_URL` in `config.h` matches the IP shown when you run the server
- Make sure your PC and ESP32 are on the same Wi-Fi network

**Device crashes on startup (only ROM boot message visible):**
- This is usually a memory issue. Make sure you are using the exact firmware from this repo without modifications that increase stack usage.

**Cards not updating after sync:**
- Scores are preserved for existing cards, reset only for new decks
- If you deleted cards on the server, they will be removed on the next sync

**WPA2-Enterprise (university Wi-Fi) not connecting:**
- Double check username and password in `config.h`
- Connection takes 6–8 seconds, which is normal for PEAP authentication
- Make sure the EAP method is PEAP/MSCHAPv2

---

## License

MIT License — free to use, modify, and distribute.
