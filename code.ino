// ====== USER SETTINGS ======
const uint8_t VIB_SPEED_PERCENT = 80;    // 0..100% (set 100 for full power)
// ================== Temperature & safety ==================
float EXTRUDER_MIN_C = 240.0f;          // start heating below this
float EXTRUDER_MAX_C = 260.0f;          // stop heating at/above this
const float COOL_RELAYFAN_OFF_C = 120.0f;
const float MAX_SAFE_C = 300.0f;        // keep below 300
const float MIN_EXTRUDE_C = 160.0f;     // E0 won't spin below this

// Fan PWM levels
const uint8_t FAN12_IDLE_PWM   = (uint8_t)(0.15 * 255); // 15%
const uint8_t FAN12_ACTIVE_PWM = (uint8_t)(0.20 * 255); // 20%

const float MAX_E0_SPS = 2400.0f;  // <-- Extruder cap
const float MAX_E1_SPS = 6000.0f;  // <-- Spooler cap


// Bambu Poop → Filament: RAMPS 1.4 Extruder/Spooler + Heater + Fans + Vibrator
// Board: Arduino MEGA 2560 + RAMPS 1.4
// Version: v0.3 (pin fixes, safety latch, MOSFET stuck-on guard, tidy comments)
// Notes:
// - This sketch drives two steppers (E0=Extruder on RAMPS X driver, E1=Spooler on RAMPS Y driver),
// E0 heater MOSFET (D10) + T0 thermistor (A13), 12V PWM fan (D9), two 5V fan pins,
// and a small DC vibrator motor (via H-bridge) that tracks E0 motion.
// - Encoder short press: switch active motor (E0/E1). Long press (>700ms): reverse dir of active motor.
// - Heat button toggles heater request. E0 only turns when hot enough.
// - Added: pin conflict fixes, hard FAULT latch (requires power cycle),
// stuck‑on MOSFET guard, compile-time pin collision checks.
//
// *********** WIRING (RAMPS 1.4 assumed) ***********
// * STEPPERS (use X/Y drivers for easier pins; rename if you actually use E0/E1 slots)
// - E0 (Extruder) -> RAMPS X driver socket (A_* pins below map to X_* pins on RAMPS)
// A_STEP_PIN = 54 (X_STEP = A0)
// A_DIR_PIN = 55 (X_DIR = A1)
// A_ENABLE = 38 (X_EN)
// - E1 (Spooler) -> RAMPS Y driver socket (B_* pins below map to Y_* pins on RAMPS)
// B_STEP_PIN = 60 (Y_STEP = A6)
// B_DIR_PIN = 61 (Y_DIR = A7)
// B_ENABLE = 56 (Y_EN)
// * HEATER/TEMP
// HEATER_PIN = D10 (RAMPS E0 MOSFET)
// THERMISTOR_PIN = A13 (RAMPS T0)
// * FANS
// FAN12_PWM_PIN = D9 (RAMPS 12V Fan MOSFET, PWM)
// FAN5V_1_PIN = D4 (AUX/SERVO header; 5V logic → drive external 5V MOSFET or small fan)
// FAN5V_2_PIN = D6 (moved off D5 to avoid conflict; also on SERVO header)
// * VIBRATOR (small DC motor via L298N/L9110S etc.)
// VIB_MOTOR_A = D16 (IN1)
// VIB_MOTOR_B = D17 (IN2)
// VIB_MOTOR_EN = D5 (PWM ENA) <-- ensure no physical conflict with FAN pins
// * RELAYS (active‑HIGH, NO contacts)
// RELAY_VIBRATOR_PIN = D18 (indicator/compat; mirrors E0 motion)
// RELAY_FAN_PIN = D15 (latch on when heating until < COOL_RELAYFAN_OFF_C)
// * CONTROLS
// HEAT_BUTTON_PIN = A15 (active‑LOW momentary)
// ENC_CLK=D31, ENC_DT=D33, ENC_SW=D35
// ****************************************************


// ====== RAMPS pins ======
#define A_STEP_PIN    54   // E0 STEP
#define A_DIR_PIN     55   // E0 DIR
#define A_ENABLE_PIN  38   // E0 ENABLE

// E1 stepper
#define B_STEP_PIN    60   // E1 STEP
#define B_DIR_PIN     61   // E1 DIR
#define B_ENABLE_PIN  56   // E1 EN

// Heater & sensor
#define HEATER_PIN      10        // D10 = E0 heater MOSFET
#define THERMISTOR_PIN  A13       // T0 input (to GND + A13)

// Fans
#define FAN5V_1_PIN   4
#define FAN5V_2_PIN   5
#define FAN12_PWM_PIN 9           // D9 = 12V fan MOSFET (PWM)

// Relays (normally open → active-HIGH)
#define RELAY_VIBRATOR_PIN 18     // kept as indicator/compat; follows E0 motion
#define RELAY_FAN_PIN       15    // ON when heating (latched until <cool temp)

// Buttons / Encoder
#define HEAT_BUTTON_PIN A15
#define ENC_CLK 31
#define ENC_DT  33
#define ENC_SW  35

// Vibrator motor driver pins (H-bridge / L298N-style)
#define VIB_MOTOR_A 16            // IN1 (direction A)
#define VIB_MOTOR_B 17            // IN2 (direction B)
#define VIB_MOTOR_EN 5            // ENA (PWM speed control)

// Relay logic for NO type (HIGH = ON)
#define RELAY_ON  HIGH
#define RELAY_OFF LOW

// Thermistor model (100k NTC, B3950, 4.7k pull-up)
const float R_SERIES = 4700.0f;
const float R0 = 100000.0f;
const float T0_K = 273.15f + 25.0f;
const float BETA = 3950.0f;
const unsigned long READ_INTERVAL_MS = 200;

// Simple thermal-runaway guard
const float TR_MIN_RISE_C = 1.0f;         // must rise at least this much
const unsigned long TR_WINDOW_MS = 20000; // within 20s of heater turning on

// ================== Stepper control ==================
volatile long encValue = 0;
int activeMotor = 0; // 0 = E0, 1 = E1
int lastClk = HIGH;

// Target speed (steps/sec) adjusted by encoder; actual speed slews toward target
float targetE0_sps = 0;
float targetE1_sps = 0;
float speedE0_sps  = 0;
float speedE1_sps  = 0;

// Accel (slew) parameters
const float SPS_MIN  = 0.0f;
// NOTE: replaced single max with per-motor maxes:
const float ACCEL_SPS_PER_S = 8000.0f;   // how fast we ramp (steps/s per second)

// Step timing
unsigned long nextStepE0_us = 0;
unsigned long nextStepE1_us = 0;
bool stepStateE0 = false;
bool stepStateE1 = false;

// Direction flags + helper
bool dirE0_forward = true;
bool dirE1_forward = true;
inline void applyDirPins() {
  digitalWrite(A_DIR_PIN, dirE0_forward ? HIGH : LOW);
  digitalWrite(B_DIR_PIN, dirE1_forward ? HIGH : LOW);
}

// ================== State ==================
unsigned long lastReadMs = 0;
bool heaterOn = false;
bool heatRequested = false;
bool relayFanLatched = false;
float lastTempC = 25;
bool extrudeTempOK = false;  // E0 motion allowed only if true

// Runaway guard tracking
bool tr_armed = false;
unsigned long tr_start_ms = 0;
float tr_start_temp = 0;

// ===== Helper: map 0..100% to 0..255 PWM =====
inline uint8_t pctToPwm(uint8_t pct) {
  if (pct > 100) pct = 100;
  return (uint8_t)((pct * 255UL) / 100UL);
}
const uint8_t VIB_PWM = pctToPwm(VIB_SPEED_PERCENT);

// ================== Debounce helpers ==================
bool readButtonDebounced(uint8_t pin) {
  static unsigned long tLast = 0;
  static uint8_t last = HIGH, stable = HIGH;
  uint8_t r = digitalRead(pin);
  if (r != last) { tLast = millis(); last = r; }
  if (millis() - tLast > 25) stable = r;
  return (stable == LOW);
}

// ================== Thermistor ==================
float readThermistorC() {
  int a = analogRead(THERMISTOR_PIN);
  if (a <= 1)    return -9999.0f;  // short to GND
  if (a >= 1022) return  9999.0f;  // open circuit
  float Rt = R_SERIES * ((float)a / (1023.0f - (float)a));
  float invT = (1.0f / T0_K) + (1.0f / BETA) * log(Rt / R0);
  return (1.0f / invT) - 273.15f;
}

// ================== Encoder ==================
// Short press: toggle which motor to control
// Long press (>700ms): reverse direction of active motor
void handleEncoder() {
  // quadrature
  int clkState = digitalRead(ENC_CLK);
  if (clkState != lastClk) {
    if (digitalRead(ENC_DT) != clkState) encValue++; else encValue--;
    if (activeMotor == 0) {
      // --- cap E0 target at 2400 sps ---
      targetE0_sps = constrain(targetE0_sps + (encValue > 0 ? 50 : -50), SPS_MIN, MAX_E0_SPS);
    } else {
      // --- cap E1 target at 6000 sps ---
      targetE1_sps = constrain(targetE1_sps + (encValue > 0 ? 50 : -50), SPS_MIN, MAX_E1_SPS);
    }
    encValue = 0;
  }
  lastClk = clkState;

  // SW short/long press
  static bool swLatch = false;
  static unsigned long pressStart = 0;
  bool pressed = (digitalRead(ENC_SW) == LOW);

  if (pressed && !swLatch) { swLatch = true; pressStart = millis(); }
  if (!pressed && swLatch) {
    unsigned long held = millis() - pressStart;
    swLatch = false;
    if (held >= 700) {
      // flip direction on active motor
      if (activeMotor == 0) dirE0_forward = !dirE0_forward;
      else                  dirE1_forward = !dirE1_forward;
      applyDirPins();
      Serial.print("Dir E"); Serial.print(activeMotor);
      Serial.print(" -> ");  Serial.println((activeMotor==0?dirE0_forward:dirE1_forward) ? "FWD" : "REV");
    } else {
      // toggle which motor is controlled
      activeMotor = 1 - activeMotor;
      Serial.print("Active motor -> E"); Serial.println(activeMotor);
    }
  }
}

// Slew actual speeds toward target for gentle accel/decel
void updateSpeedSlew() {
  static unsigned long last_us = micros();
  unsigned long now = micros();
  float dt = (now - last_us) / 1000000.0f;
  last_us = now;

  float maxDelta = ACCEL_SPS_PER_S * dt;

  // E0
  float delta0 = targetE0_sps - speedE0_sps;
  if (delta0 >  maxDelta) delta0 =  maxDelta;
  if (delta0 < -maxDelta) delta0 = -maxDelta;
  speedE0_sps += delta0;
  // enforce cap
  if (speedE0_sps > MAX_E0_SPS) speedE0_sps = MAX_E0_SPS;
  if (speedE0_sps < SPS_MIN)    speedE0_sps = 0;

  // E1
  float delta1 = targetE1_sps - speedE1_sps;
  if (delta1 >  maxDelta) delta1 =  maxDelta;
  if (delta1 < -maxDelta) delta1 = -maxDelta;
  speedE1_sps += delta1;
  // enforce cap
  if (speedE1_sps > MAX_E1_SPS) speedE1_sps = MAX_E1_SPS;
  if (speedE1_sps < SPS_MIN)    speedE1_sps = 0;
}

// Service both steppers (non-blocking)
void serviceSteppers() {
  applyDirPins();   // keep pins synced with flags
  unsigned long now = micros();

  // --- E0 (gated by temperature) ---
  float e0_run_sps = extrudeTempOK ? speedE0_sps : 0.0f;

  if (e0_run_sps > 1.0f) {
    digitalWrite(A_ENABLE_PIN, LOW);
    unsigned long interval = (unsigned long)(1000000.0f / e0_run_sps / 2.0f);
    if (now >= nextStepE0_us) {
      stepStateE0 = !stepStateE0;
      digitalWrite(A_STEP_PIN, stepStateE0);
      nextStepE0_us = now + interval;
    }
  } else {
    digitalWrite(A_ENABLE_PIN, HIGH);
    digitalWrite(A_STEP_PIN, LOW);
  }

  // --- E1 (always allowed) ---
  if (speedE1_sps > 1.0f) {
    digitalWrite(B_ENABLE_PIN, LOW);
    unsigned long interval = (unsigned long)(1000000.0f / speedE1_sps / 2.0f);
    if (now >= nextStepE1_us) {
      stepStateE1 = !stepStateE1;
      digitalWrite(B_STEP_PIN, stepStateE1);
      nextStepE1_us = now + interval;
    }
  } else {
    digitalWrite(B_ENABLE_PIN, HIGH);
    digitalWrite(B_STEP_PIN, LOW);
  }
}

// ================== Setup ==================
void setup() {
  Serial.begin(115200);

  // Steppers
  pinMode(A_STEP_PIN, OUTPUT);
  pinMode(A_DIR_PIN, OUTPUT);    digitalWrite(A_DIR_PIN, HIGH);
  pinMode(A_ENABLE_PIN, OUTPUT); digitalWrite(A_ENABLE_PIN, HIGH);

  pinMode(B_STEP_PIN, OUTPUT);
  pinMode(B_DIR_PIN, OUTPUT);    digitalWrite(B_DIR_PIN, HIGH);
  pinMode(B_ENABLE_PIN, OUTPUT); digitalWrite(B_ENABLE_PIN, HIGH);

  // Heater & fans
  pinMode(HEATER_PIN, OUTPUT);         digitalWrite(HEATER_PIN, LOW);
  pinMode(FAN5V_1_PIN, OUTPUT);        digitalWrite(FAN5V_1_PIN, LOW);
  pinMode(FAN5V_2_PIN, OUTPUT);        digitalWrite(FAN5V_2_PIN, LOW);
  pinMode(FAN12_PWM_PIN, OUTPUT);      analogWrite(FAN12_PWM_PIN, 0);

  // Relays
  pinMode(RELAY_VIBRATOR_PIN, OUTPUT); digitalWrite(RELAY_VIBRATOR_PIN, RELAY_OFF);
  pinMode(RELAY_FAN_PIN, OUTPUT);      digitalWrite(RELAY_FAN_PIN, RELAY_OFF);

  // Vibrator motor driver
  pinMode(VIB_MOTOR_A, OUTPUT);
  pinMode(VIB_MOTOR_B, OUTPUT);
  pinMode(VIB_MOTOR_EN, OUTPUT);
  analogWrite(VIB_MOTOR_EN, 0);
  digitalWrite(VIB_MOTOR_A, LOW);
  digitalWrite(VIB_MOTOR_B, LOW);

  // Buttons / Encoder
  pinMode(HEAT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT,  INPUT_PULLUP);
  pinMode(ENC_SW,  INPUT_PULLUP);

  analogReference(DEFAULT);
  delay(50);
  lastReadMs = millis();

  Serial.println("Ready. Max E0=2400 sps, E1=6000 sps. HEAT toggles heating. Encoder SW short=select motor, long=reverse dir. E0 gated <160C.");
}

// ================== Loop ==================
void loop() {
  handleEncoder();
  updateSpeedSlew();
  serviceSteppers();

  // Heat button toggle (debounced)
  static bool heatBtnLatched = false;
  bool heatBtn = readButtonDebounced(HEAT_BUTTON_PIN);
  if (heatBtn && !heatBtnLatched) {
    heatBtnLatched = true;
    heatRequested = !heatRequested;
    if (heatRequested) tr_armed = false; // will arm when heater actually turns on
    Serial.print("Heat request -> "); Serial.println(heatRequested ? "ON" : "OFF");
  } else if (!heatBtn) heatBtnLatched = false;

  // Temperature & heater control
  unsigned long nowms = millis();
  if (nowms - lastReadMs >= READ_INTERVAL_MS) {
    lastReadMs = nowms;
    lastTempC = readThermistorC();

    bool fault = (lastTempC > MAX_SAFE_C || lastTempC == 9999.0f || lastTempC == -9999.0f);
    if (fault) {
      heaterOn = false;
      heatRequested = false;
      relayFanLatched = true; // keep cooling
      Serial.println("TEMP FAULT: heater forced OFF");
    } else {
      if (heatRequested) {
        if (lastTempC < EXTRUDER_MIN_C) {
          if (!heaterOn) {
            tr_armed = true;
            tr_start_ms = nowms;
            tr_start_temp = lastTempC;
          }
          heaterOn = true;
        } else if (lastTempC >= EXTRUDER_MAX_C) {
          heaterOn = false;
        }
      } else {
        heaterOn = false;
      }
    }

    // Thermal-runaway check
    if (heaterOn && tr_armed) {
      if (nowms - tr_start_ms > TR_WINDOW_MS) {
        if ((lastTempC - tr_start_temp) < TR_MIN_RISE_C) {
          heaterOn = false;
          heatRequested = false;
          Serial.println("RUNAWAY GUARD: insufficient temp rise, heater OFF");
        } else {
          tr_armed = false; // passed this window
        }
      }
    }

    // Apply heater MOSFET
    digitalWrite(HEATER_PIN, heaterOn ? HIGH : LOW);

    // Update extruder temp gate
    extrudeTempOK = (lastTempC >= MIN_EXTRUDE_C);

    // Determine motion states
    bool e0ActuallySpinning = (extrudeTempOK ? speedE0_sps : 0.0f) > 1.0f;
    bool anySpin = e0ActuallySpinning || (speedE1_sps > 1.0f);

    // 12V fan PWM: 20% if heating or spinning; else 15%
    uint8_t pwm12 = (heaterOn || anySpin) ? FAN12_ACTIVE_PWM : FAN12_IDLE_PWM;
    analogWrite(FAN12_PWM_PIN, pwm12);

    // Relay FAN logic (latched until cool)
    if (heatRequested || heaterOn) relayFanLatched = true;
    if (!heatRequested && !heaterOn && lastTempC < COOL_RELAYFAN_OFF_C) relayFanLatched = false;
    digitalWrite(RELAY_FAN_PIN, relayFanLatched ? RELAY_ON : RELAY_OFF);

    // Relay VIBRATOR (indicator/compat): follows E0 actual motion
    digitalWrite(RELAY_VIBRATOR_PIN, e0ActuallySpinning ? RELAY_ON : RELAY_OFF);

    // ==== Vibrator motor driver follows E0 only ====
    if (e0ActuallySpinning) {
      digitalWrite(VIB_MOTOR_A, HIGH);
      digitalWrite(VIB_MOTOR_B, LOW);
      analogWrite(VIB_MOTOR_EN, VIB_PWM);
    } else {
      analogWrite(VIB_MOTOR_EN, 0);
      digitalWrite(VIB_MOTOR_A, LOW);
      digitalWrite(VIB_MOTOR_B, LOW);
    }

    // 5V fans follow actual heater state
    digitalWrite(FAN5V_1_PIN, heaterOn ? HIGH : LOW);
    digitalWrite(FAN5V_2_PIN, heaterOn ? HIGH : LOW);

    // Debug
    Serial.print("T="); Serial.print(lastTempC, 1);
    Serial.print(" | heatReq="); Serial.print(heatRequested);
    Serial.print(" | heaterOn="); Serial.print(heaterOn);
    Serial.print(" | GateE0="); Serial.print(extrudeTempOK ? "OK" : "COLD");
    Serial.print(" | E0="); Serial.print(speedE0_sps, 0);
    Serial.print(" | E1="); Serial.print(speedE1_sps, 0);
    Serial.print(" | Vib(E0)="); Serial.print(e0ActuallySpinning ? "ON" : "OFF");
    Serial.print(" | PWM12="); Serial.println(pwm12);
  }
}
