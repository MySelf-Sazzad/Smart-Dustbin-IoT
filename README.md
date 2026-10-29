# Smart Dustbin IoT Monitoring System ♻️

An IoT-based automated Smart Dustbin with real-time waste classification, full bin detection, and live monitoring via Google Firebase Realtime Database and a modern web dashboard.

---

## 🚀 Features

- **Automated Waste Classification**:
  - **Metal Waste**: Detected via Inductive Proximity Sensor.
  - **Wet Waste**: Differentiated via Rain Sensor ADC reading.
  - **Dry Waste**: Categorized when no metal or moisture is present.
- **Smart Actuation**:
  - **Stepper Motor (28BYJ-48 + ULN2003)**: Rotates platform to the appropriate bin compartment.
  - **Servo Motor Gate**: Opens to drop waste only when the target bin has available space; remains strictly locked if the bin is full.
  - **Active Buzzer**: Sounds acoustic alarms upon waste drops or when full bins are detected.
- **Full Bin Detection**:
  - 3 IR Sensors monitor Metal, Dry, and Wet bins.
  - If any bin is full, drops to that bin are blocked, an audible alarm triggers, and the dashboard immediately updates to **FULL**.
- **Real-Time Web Dashboard**:
  - Built with pure HTML5, CSS3, and dynamic Firebase Realtime Database Web SDK.
  - Real-time waste counters, bucket status badges, and live session chart for rain sensor readings.
  - Hosted directly on **Vercel**.

---

## 🛠️ Hardware Pinout (ESP32 DevKit)

| Component | Pin / GPIO | Description |
|---|---|---|
| **IR Top Presence Sensor** | GPIO 34 | Detects incoming waste at hatch |
| **IR Full Sensor (Metal)** | GPIO 35 | Full level detector for Metal bin |
| **IR Full Sensor (Dry)** | GPIO 32 | Full level detector for Dry bin |
| **IR Full Sensor (Wet)** | GPIO 33 | Full level detector for Wet bin |
| **Inductive Metal Sensor** | GPIO 25 | Proximity detection for metallic waste |
| **Rain Sensor (AO)** | GPIO 36 | Analog moisture/rain level (0 - 4095) |
| **Servo Motor (Gate)** | GPIO 19 | Gate open / close actuation |
| **Active Buzzer** | GPIO 23 | Audio alarms and notifications |
| **Stepper Motor (ULN2003)** | GPIO 16, 17, 21, 22 | IN1, IN2, IN3, IN4 platform rotation |

---

## 🌐 Cloud Architecture

```
[ ESP32 Hardware ]
        │
   (Wi-Fi / HTTPS)
        ▼
[ Google Firebase Realtime Database ]
        │
    (WebSocket)
        ▼
[ Vercel Web Dashboard (index.html) ]
```

---

## 💻 Deployment to Vercel

1. Push this repository to GitHub.
2. In [Vercel](https://vercel.com), click **Add New > Project**.
3. Select this repository and click **Deploy**.
4. The site will be live instantly with zero configuration!

