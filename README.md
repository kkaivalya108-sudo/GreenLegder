# GreenLedger — AI-Based Carbon Credit Monitoring System

> BTech Final Year Project | Vishwakarma Institute of Technology, Pune  
> Guide: Prof. Prachi Kamlesh Sorte

---

## What is GreenLedger?

GreenLedger is a full-stack IoT + AI platform that enables small Indian farmers to monitor carbon sequestration on their land and earn verified carbon credits — an income stream currently inaccessible to them due to high verification costs.

**Stack:** Python Flask · MongoDB · scikit-learn Random Forest · ESP32 · HTML/CSS/JS · Docker

---

## Run in 3 Commands (Docker — works anywhere)

```bash
git clone https://github.com/YOUR_USERNAME/greenledger.git
cd greenledger
docker compose up --build
```

Then open **http://localhost:8080** in your browser.

> Docker will automatically: start MongoDB, train the AI model, seed demo data, and serve the dashboard.

---

## Run Without Docker (Windows / Mac / Linux)

### Prerequisites
- Python 3.11+
- MongoDB Community running on port 27017 — download at https://www.mongodb.com/try/download/community

### Steps

```bash
# 1. Clone
git clone https://github.com/YOUR_USERNAME/greenledger.git
cd greenledger

# 2. Install Python dependencies
cd backend
pip install -r requirements.txt

# 3. Train the AI model
python model/train_model.py

# 4. Seed demo data into MongoDB
python seed_db.py

# 5. Start Flask API
python app.py
```

Open a second terminal:
```bash
# 6. Serve the frontend (Python built-in server)
cd greenledger/frontend
python -m http.server 8080
```

Open **http://localhost:8080** — the dashboard connects to the API at port 5000.

---

## Project Structure

```
greenledger/
├── docker-compose.yml        ← one-command deployment
├── nginx.conf                ← frontend proxy config
├── .github/workflows/ci.yml  ← GitHub Actions CI
│
├── backend/
│   ├── Dockerfile
│   ├── requirements.txt
│   ├── app.py                ← Flask entry point
│   ├── seed_db.py            ← populate demo data
│   ├── routes/
│   │   ├── sensor_routes.py  ← IoT data ingestion
│   │   ├── farm_routes.py    ← farm CRUD
│   │   ├── credit_routes.py  ← carbon credit ledger
│   │   └── ai_routes.py      ← AI estimation endpoints
│   └── model/
│       ├── train_model.py    ← train Random Forest
│       └── predictor.py      ← inference helper
│
├── frontend/
│   └── index.html            ← full dashboard (single file)
│
└── esp32/
    └── greenledger_esp32_v2_1/ ← ESP32 firmware (ThingSpeak upload)
```

---

## API Reference

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/health` | Health check |
| POST | `/api/sensors/data` | ESP8266 pushes reading |
| GET | `/api/sensors/latest` | Latest reading per farm |
| GET | `/api/sensors/history/<farm_id>` | Reading history |
| GET | `/api/sensors/alerts` | Active alerts |
| GET | `/api/farms/` | List all farms |
| POST | `/api/farms/` | Register new farm |
| PATCH | `/api/farms/<id>/ndvi` | Update NDVI |
| GET | `/api/credits/` | All carbon credits |
| POST | `/api/credits/issue` | Issue a credit |
| PATCH | `/api/credits/<id>/verify` | Verify a credit |
| GET | `/api/credits/summary` | Totals by status |
| POST | `/api/ai/estimate` | Estimate CO₂ |
| POST | `/api/ai/estimate/farm/<id>` | Estimate for stored farm |

**Example — push sensor reading:**
```bash
curl -X POST http://localhost:5000/api/sensors/data \
  -H "Content-Type: application/json" \
  -d '{"farm_id":"F-01","soil_moisture":62,"temperature":28.4,"humidity":71,"device_id":"GL-8266"}'
```

---

## ESP32 Hardware Setup and ThingSpeak

The ESP32 preserves the current sensor fields in ThingSpeak and adds the model's estimated carbon-credit count as Field 5. Soil moisture and temperature are also sent to the GreenLedger AI endpoint as model inputs. If the AI endpoint is unavailable, sensor fields still upload and Field 5 is left empty.

### Create and configure a ThingSpeak channel

1. Keep your current channel fields and add/enable Field 5:
   - Field 1: Soil Moisture (%)
   - Field 2: Temperature (°C)
   - Field 3: Humidity (%)
   - Field 4: Estimated SOC (%) (moisture-based proxy)
   - Field 5: Estimated Carbon Credits (credits)
2. Save the channel, then open **API Keys** and copy its **Write API Key** (not the Channel ID or Read API Key).
3. Copy `esp32/greenledger_esp32_v2_1/secrets.example.h` to `esp32/greenledger_esp32_v2_1/secrets.h`. Set WiFi, the ThingSpeak Write API Key, and `AI_ESTIMATE_URL` to your laptop's local API address as described below.
4. Set the farm's NDVI (0–1), area in hectares, and soil organic carbon (%) in `secrets.h`. These are model inputs; the existing Field 4 SOC proxy is not used as a measured SOC value.
5. Select **DOIT ESP32 DEVKIT V1** in Arduino IDE and upload the sketch. Install **ArduinoJson 6**, **DHT sensor library** by Adafruit, and **Adafruit Unified Sensor**. Open Serial Monitor at 115200 baud; a positive ThingSpeak entry response confirms upload.

ThingSpeak's free channel tier requires at least 15 seconds between updates; this sketch sends every 30 seconds. Keep the Write API Key private. If ThingSpeak replies with `0`, check the key and channel fields, and make sure the update interval is not too short. These are model estimates, not verified or issued carbon credits.

### Components
- DOIT ESP32 DEVKIT V1
- Capacitive soil moisture sensor (or resistive)
- DHT11 temperature + humidity sensor
- Breadboard + jumper wires

### Wiring

```
ESP32    →  Sensor
─────────────────────────────
GPIO34   →  Soil moisture AOUT
GPIO15   →  DHT11 DATA
3.3V     →  VCC (both sensors)
GND      →  GND (both sensors)
```

### Flash the firmware

1. Install [Arduino IDE](https://www.arduino.cc/en/software)
2. Install the ESP32 board package in Arduino IDE if needed.
3. Install via Library Manager: **DHT sensor library** by Adafruit and **Adafruit Unified Sensor**.
4. Open `esp32/greenledger_esp32_v2_1/greenledger_esp32_v2_1.ino`.
5. Copy `secrets.example.h` to `secrets.h` in the same folder and fill in WiFi, ThingSpeak, local API URL, and farm inputs there.
6. Install **ArduinoJson 6**, **DHT sensor library** by Adafruit, and **Adafruit Unified Sensor**.
7. Select **DOIT ESP32 DEVKIT V1** and upload.

---

## AI Model

| Property | Value |
|----------|-------|
| Algorithm | Random Forest Regressor |
| Trees | 300 |
| Features | NDVI, area, SOC%, avg temp, avg moisture, months, practice, practice years, soil type, irrigation, season |
| Target | CO₂ sequestered (tonnes) |
| Training data | Synthetic generated samples (not field-validated) |
| R² / MAE | Printed at training time; synthetic holdout metrics are not real-world accuracy |
| Carbon formula | `Credits = floor(CO₂_tonnes)` · 1 credit = 1 tonne |

The model is a Random Forest trained on synthetic data, so estimates are prototype outputs and are not verified or issued carbon credits. The local API trains it from `backend/model/train_model.py` before serving requests.

### Run the model API locally (no Render)

The ESP32 calls a small, database-free API on your laptop. Render is not used, but the laptop must remain on and connected to the same WiFi as the ESP32.

1. On Windows, open PowerShell and run:
   ```powershell
   cd "C:\Users\Kaivalya Kulkarni\GreenLegder\backend"
   py -3.11 -m pip install -r requirements-ai-local.txt
   py -3.11 model\train_model.py
   py -3.11 ai_app.py
   ```
2. Keep that PowerShell window open while the ESP32 is operating. The API listens on port 5001.
3. Run `ipconfig` and find the laptop's IPv4 address on the same WiFi network as the ESP32.
4. Set `AI_ESTIMATE_URL` in `esp32/greenledger_esp32_v2_1/secrets.h` to `http://<laptop-ipv4>:5001/api/ai/estimate`.
5. If Windows Firewall asks, allow Python on **Private networks**. Do not allow it on public networks.
6. Recompile and upload the ESP32 sketch. If the laptop's IP changes, update `AI_ESTIMATE_URL` and upload again.

---

## Team

| Name | Role |
|------|------|
| Ram Sunil Rajurkar | AI model |
| Chirag Mahavir Kochar | Frontend dashboard |
| Atharva Satish Gaikwad | ESP8266 firmware + hardware + Backend + Database |
| Prem Mangesh Palhade | Deployment + App Development |

**Guide:** Prof. Prachi Kamlesh Sorte  
**Institution:** Vishwakarma Institute of Technology, Pune
