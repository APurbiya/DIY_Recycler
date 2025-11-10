# ♻️ Bambu Poop Recycler – Desktop Filament Extruder

Converting **Bambu Poop** (failed prints / support waste) back into **usable filament** using a **screw-based extruder**.  
The goal: a **compact, desktop-friendly recycler** that handles the entire process — **shredding → extruding → spooling** — all from one enclosure.

---

## 🧩 System Overview

| Component | Function |
|------------|-----------|
| **Arduino Mega + RAMPS 1.4** | Controls heater, thermistor, fans, vibrator, and 2 stepper drivers (extruder screw + spooler). |
| **Drill Motor + Motor Controller (e.g., DC PWM driver)** | Drives shredder or auger screw for initial granulation. |
| **12 V PSU (≥ 20 A)** | Main power source for heaters, fans, steppers, and motor controller. |
| **Buck Converter (12 V → 5 V)** | Powers ESP32 dashboard and 5 V logic fans. |
| **ESP32** | Monitors Mega’s serial output and serves real-time data on a local webpage. |

---


## ⚡️ Power Distribution

| Source | Destination | Description |
|---------|--------------|--------------|
| **12 V DC PSU (20 A min)** | **RAMPS 1.4 (11 A + 5 A inputs)** | Main power for heater, fans, and steppers |
|  | **DC Motor Controller (for drill motor)** | Supplies high-current 12 V for shredding/auger motor |
|  | **Buck Converter (input)** | Feeds 12 V into buck regulator |
|  | → **5 V Output (from Buck Converter)** | Powers ESP32 + 5 V logic fans |
| **Ground (GND)** | All components common | Connect PSU GND ↔ RAMPS GND ↔ Buck GND ↔ Motor Controller GND ↔ ESP32 GND |



✅ **Tip:** all grounds **must** be common (PSU GND ↔ RAMPS GND ↔ Buck GND ↔ Motor Controller GND ↔ ESP32 GND).


---

## 🧵 RAMPS 1.4 Wiring Guide

| Function | RAMPS Pins | Connected Component | Notes |
|-----------|-------------|---------------------|-------|
| **Heater** | D10 | Extruder heater cartridge | MOSFET output, 12 V |
| **Thermistor (T0)** | T0 (A13) | 100 k NTC thermistor | Connect one leg to T0, one to GND |
| **Stepper A (E0)** | X-driver socket (A_STEP=54, A_DIR=55, A_EN=38) | Extruder stepper | Drives screw |
| **Stepper B (E1)** | Y-driver socket (B_STEP=60, B_DIR=61, B_EN=56) | Spooler stepper | Controls filament winding |
| **Fans (12 V)** | D9 (PWM MOSFET) | 12 V cooling fan | Variable speed based on temp/activity |
| **Fans (5 V)** | D4, D6 | Small logic fans | Driven via MOSFET or transistor boards |
| **Vibrator / Mixer Motor** | D16/D17/D5 → L298N IN1/IN2/ENA | Vibrating base / mixer | PWM controlled |
| **Heat Toggle Button** | A15 | Momentary push button → GND | Toggles heating cycle |
| **Encoder** | D31 (CLK), D33 (DT), D35 (SW) | Rotary encoder | Speed + direction control |
| **ESP32 Link** | TX0 (pin 1) → RX (via 1 k/2 k divider) | ESP32 RX pin | Sends data for web dashboard |
| | RX0 (pin 0) ← TX (3.3 V) | ESP32 TX pin | Optional (disconnect when uploading) |

---

## ⚙️ 12 V PSU → Component Wiring

| Output | Destination | Wire Gauge | Notes |
|---------|-------------|------------|-------|
| +12 V | RAMPS Power Inputs (11 A & 5 A) | 14–16 AWG | Use ferrules or ring terminals |
| +12 V | Drill Motor Controller V+ | 14 AWG | Feeds high-current DC motor |
| +12 V | Buck Converter IN+ | 18 AWG | For ESP32 + 5 V logic |
| GND | Common to all | 14–18 AWG | Must tie all GNDs together |

### Buck Converter
| Input | Output | Use |
|--------|---------|-----|
| 12 V IN + / – | 5 V OUT + / – | Powers ESP32, small 5 V fans and L298N |

### Drill Motor Controller
| Input | Output | Use |
|--------|---------|-----|
| +12 V / GND | M+ / M– | Connects to drill/auger motor |
| PWM / POT / CTRL | Optional | Speed control (manual or logic-level) |

---

## 🪛 Assembly Tips

- **Full assembly guide will me added SOON**
- Keep **heater + thermistor wires** away from **motor leads** to avoid noise.
- Mount the **buck converter** near the ESP32; keep its output wires short.
- Label all 12 V lines before final crimping.
- Always test the **heater MOSFET** and **fan MOSFETs** with a multimeter before loading filament.
- Add a **thermal fuse (250 °C)** inline with the heater block for safety.

---

## 🧯 Power Safety

- Use a **12 V 20 A (240 W) PSU** with built-in short-circuit protection.
- Add a **5 A inline fuse** for the RAMPS input and **10 A fuse** for the motor controller branch.
- Ensure metal enclosures are **earth-grounded**.
- Never leave the extruder unattended while heating.

---

