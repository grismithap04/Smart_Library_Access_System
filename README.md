# Smart Library Access System

A fingerprint-based library entry and exit tracking system built. Replaces the manual log book process with an automated biometric system, real-time dashboards for the librarian and hostel wardens, and automatic roll call context for students who stay back at the library after curfew hours.

---

## The Problem

Girl students who use the library after 6:30 PM must sign a paper log book at entry (before 6:10 PM) and again at exit (8:00 PM), after which they receive a physical log sheet to submit at the hostel gate. During exam seasons, the exit queue runs from 7:50 PM and students receive their sheets as late as 8:15 PM. Walking time to the hostel adds another 6–18 minutes.

This system eliminates the queues, the paper.

---

## How It Works

```
Student taps finger on R307 sensor
        ↓
ESP32 reads fingerprint slot ID
        ↓
ESP32 calls Flask: GET /api/fingerprint-lookup?fp_slot=X
        ↓
Flask returns student name and reg_id from Oracle
        ↓
ESP32 checks /api/active for open session
        ↓
If no open session → POST entry to /api/mock-fingerprint
If open session exists → POST exit to /api/mock-fingerprint
        ↓
Flask writes to Oracle lib_log table
        ↓
OLED shows name + action, LED blinks, buzzer beeps
        ↓
Both dashboards update on next 15-second refresh
```

**Entry cutoff:** Fingerprint entry is blocked after 6:10 PM. The OLED shows "ENTRY CLOSED", the red LED flashes three times, and the buzzer fires three long beeps.

**Auto-exit:** At 8:10 PM, a background scheduler in Flask automatically closes all open sessions and marks them as `auto-exit` in the database.

**Enrollment:** New students tap the sensor → red LED and "NOT FOUND" on OLED → librarian enters their reg_id on the dashboard → dashboard queues enrollment → ESP32 polls the queue, detects the request, runs the two-placement enrollment flow, and saves the slot mapping to Oracle.

---

## Tech Stack

| Layer | Technology |
|---|---|
| Microcontroller | ESP32 (Arduino C++) |
| Fingerprint sensor | R307S optical sensor |
| Feedback | SSD1306 OLED, Green LED, Red LED, Passive buzzer |
| Backend | Python — Flask, cx_Oracle, Flask-CORS |
| Database | Oracle XE 21c |
| Frontend | Vanilla HTML, CSS, JavaScript, Tabler Icons |
| Communication | HTTP REST API over local WiFi |

---

## Project Structure

```
smart-library-access/
│
├── app.py                          # Flask backend — all API routes + scheduler
│
├── lib_dash/
│   ├── lib_dash.html               # Librarian dashboard
│   ├── lib_dash.css
│   └── lib_dash.js
│
├── ward_dash/
│   ├── ward_dash.html              # Warden dashboard
│   ├── ward_dash.css
│   └── ward_dash.js
│
├── esp32/
│   └── sastra_fingerprint.ino      # ESP32 firmware
│
└── sql/
    └── schema.sql                  # Oracle table definitions + seed data
```

---

## Database Schema

```sql
-- Master student list (pre-loaded from college records)
CREATE TABLE students (
    reg_id      VARCHAR2(20) PRIMARY KEY,
    name        VARCHAR2(100) NOT NULL,
    dept        VARCHAR2(50),
    course      VARCHAR2(50),
    year        NUMBER(1),
    hostel_code VARCHAR2(10),
    room_no     VARCHAR2(20),
    phone       VARCHAR2(15)
);

-- Maps sensor slot ID to student register number
CREATE TABLE fingerprint_map (
    fp_slot     NUMBER PRIMARY KEY,
    reg_id      VARCHAR2(20) UNIQUE REFERENCES students(reg_id),
    enrolled_at TIMESTAMP DEFAULT SYSTIMESTAMP
);

-- Entry and exit log — one row per library session
CREATE TABLE lib_log (
    log_id        NUMBER GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    reg_id        VARCHAR2(20) REFERENCES students(reg_id),
    log_date      DATE DEFAULT TRUNC(SYSDATE),
    entry_time    TIMESTAMP,
    exit_time     TIMESTAMP,              -- NULL = student currently inside
    source        VARCHAR2(20),           -- biometric | manual | force-exit | auto-exit
    manual_reason VARCHAR2(200)
);

-- Roll call status per student per day
CREATE TABLE hostel_attendance (
    att_id         NUMBER GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    reg_id         VARCHAR2(20) REFERENCES students(reg_id),
    att_date       DATE DEFAULT TRUNC(SYSDATE),
    att_time       TIMESTAMP,
    status         VARCHAR2(20),          -- PRESENT | LATE | IN_LIBRARY
    library_log_id NUMBER REFERENCES lib_log(log_id),
    message_sent   CHAR(1) DEFAULT 'N',
    verified_by    VARCHAR2(30)
);
```

---

## Hardware Wiring

### R307 Fingerprint Sensor → ESP32

| R307 Pin | ESP32 Pin |
|---|---|
| TX | GPIO 16 (RX2) |
| RX | GPIO 17 (TX2) |
| VCC | 3.3V |
| GND | GND |

### SSD1306 OLED → ESP32

| OLED Pin | ESP32 Pin |
|---|---|
| SDA | GPIO 21 |
| SCL | GPIO 22 |
| VCC | 3.3V |
| GND | GND |

### LEDs and Buzzer → ESP32

| Component | ESP32 Pin | Note |
|---|---|---|
| Green LED (+) | GPIO 26 | Via 220Ω resistor to GND |
| Red LED (+) | GPIO 27 | Via 220Ω resistor to GND |
| Buzzer (+) | GPIO 25 | Passive buzzer |
| All (−) | GND | Common ground |

---

## API Endpoints

| Method | Endpoint | Purpose |
|---|---|---|
| GET | `/api/active` | All students currently in library |
| GET | `/api/today-log` | Full day log with entry, exit, duration, source |
| GET | `/api/students` | All registered students |
| GET | `/api/student/<reg_id>` | Individual student lookup |
| GET | `/api/hostel/<code>/active` | Hostel-specific active students |
| GET | `/api/hostel/<code>/returned` | Hostel-specific returned students |
| GET | `/api/fingerprint-lookup` | Resolve fingerprint slot ID to student |
| GET | `/api/auto-exit-status` | Check if auto-exit has run today |
| POST | `/api/mock-fingerprint` | Record entry or exit from ESP32 |
| POST | `/api/force-exit` | Manually close an open session |
| POST | `/api/auto-exit` | Exit all students (manual trigger) |
| POST | `/api/manual-entry` | Librarian manual entry or exit |
| POST | `/api/request-enrollment` | Dashboard queues fingerprint enrollment |
| GET | `/api/enrollment-queue` | ESP32 polls for pending enrollment |
| POST | `/api/complete-enrollment` | Save slot-to-student mapping after enrollment |
| GET | `/api/enroll-status/<reg_id>` | Check enrollment status |
| POST | `/api/enrollment-failed` | Notify server of failed enrollment |

---

## Setup and Running

### Prerequisites

```
Python 3.8+
Oracle XE 21c installed and listener running
cx_Oracle, Flask, Flask-CORS installed
Arduino IDE with ESP32 board support
Adafruit Fingerprint, Adafruit SSD1306, ArduinoJson libraries installed
```

### Install Python dependencies

```bash
pip install flask flask-cors cx_Oracle
```

### Start Oracle

```powershell
lsnrctl start
```

Verify with `lsnrctl status` — Service "XE" must show status READY.

### Create database tables

Connect to Oracle via SQL Developer or SQL*Plus and run `sql/schema.sql`.

### Configure the ESP32 firmware

Open `esp32/sastra_fingerprint.ino` in Arduino IDE and update:

```cpp
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* BASE     = "http://YOUR_LAPTOP_IP:5000";
```

Find your laptop IP with `ipconfig` (Windows) or `ifconfig` (Linux/Mac) — use the IPv4 address under your WiFi adapter.

Upload to ESP32. Open Serial Monitor at 115200 baud.

### Start Flask

```bash
cd smart-library-access
python app.py
```

Flask runs on port 5000. Keep the terminal open.

### Open dashboards

| Dashboard | URL |
|---|---|
| Librarian | http://localhost:5000/ |
| Warden | http://localhost:5000/warden |
| API health check | http://localhost:5000/api/test |

---

## ESP32 Serial Commands

With the ESP32 connected and Serial Monitor open at 115200 baud:

| Command | Action |
|---|---|
| `scan` | Manual fingerprint scan |
| `delete` | Delete a fingerprint slot (prompts for slot number) |
| `list` | List all enrolled slots |
| `wifi` | Show WiFi connection status and IP |
| `help` | Show command reference |

Enrollment is done from the librarian dashboard, not from the Serial Monitor.

---

## Feedback Patterns

| Event | Green LED | Red LED | Buzzer | OLED |
|---|---|---|---|---|
| Successful entry | Blinks twice | Off | Short beep | ENTRY + name |
| Successful exit | Blinks twice | Off | Short beep | EXIT + name |
| Unrecognised fingerprint | Off | Flashes twice | Two short beeps | NOT FOUND |
| Entry blocked after 6:10 PM | Off | Flashes three times | Three long beeps | ENTRY CLOSED |
| Server error | Off | Flashes twice | None | Server Error |

---

## Timing Rules

| Rule | Time | Behaviour |
|---|---|---|
| Entry cutoff | 6:10 PM | New entries blocked — biometric and manual |
| Roll call window | 5:30–6:40 PM | Warden dashboard shows roll call banner |
| Library closing | 8:00 PM | Library staff ask students to leave |
| Auto-exit | 8:10 PM | All open sessions closed automatically by scheduler |
| Auto-exit reset | Midnight | Flag resets for the next day |

---

## Known Limitations

- R307 sensor supports a maximum of 162 fingerprint templates. For institution-wide deployment, a USB scanner with database-stored templates (ZK4500 or Secugen Hamster Pro) is recommended.
- The enrollment queue uses in-memory storage in Flask. Restarting Flask clears any pending enrollment requests.
- Roll call integration with the hostel biometric system is not yet implemented — it requires coordination with the institution's IT team.
- The system runs on the local network only. No internet dependency.

---

## Future Scope

- Roll call integration — query lib_log at roll call time and suppress the late mark for students with open library sessions
- Student self-service portal — students check their own session history over campus WiFi
- Analytics dashboard — peak usage hours, hostel-wise participation, exam season patterns
- Multiple entry points — second sensor for a second library entrance, both writing to the same database
- Offline resilience — ESP32 logs locally when WiFi is unavailable and syncs on reconnection
- Configurable timings — entry cutoff and auto-exit time as environment variables

---

## Built With

Internship project at **DIYA — Do It Yourself Academy**
July 2026
