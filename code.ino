// ====== RAMPS pins ======
#define A_STEP_PIN    54   // E0 STEP
#define A_DIR_PIN     55   // E0 DIR
#define A_ENABLE_PIN  38   // E0 ENABLE

// E1 stepper
#define B_STEP_PIN    26   // E1 STEP
#define B_DIR_PIN     28   // E1 DIR
#define B_ENABLE_PIN  24   // E1 ENABLE

// Heater & sensor
#define HEATER_PIN      10        // D10 = E0 heater MOSFET
#define THERMISTOR_PIN  A13       // T0 input (to GND + A13)

// Fans
#define FAN5V_1_PIN   4
#define FAN5V_2_PIN   5
#define FAN12_PWM_PIN 9           // D9 = 12V fan MOSFET (PWM)

// Relays (normally open → active-HIGH)
#define RELAY_VIBRATOR_PIN 18     // ON when any stepper spins
#define RELAY_FAN_PIN       15    // ON when heating (latched until <90°C)

// Buttons / Encoder
#define HEAT_BUTTON_PIN A15
#define ENC_CLK 31
#define ENC_DT  33
#define ENC_SW  35

// Relay logic for NO type (HIGH = ON)
#define RELAY_ON  HIGH
#define RELAY_OFF LOW

// Temperature settings
float EXTRUDER_MIN_C = 250.0f;
float EXTRUDER_MAX_C = 280.0f;
const float COOL_RELAYFAN_OFF_C = 90.0f;
const float MAX_SAFE_C = 300.0f;
const unsigned long READ_INTERVAL_MS = 200;

// Thermistor model (100k NTC, B3950, 4.7k pull-up)
const float R_SERIES = 4700.0f;
const float R0 = 100000.0f;
const float T0_K = 273.15f + 25.0f;
const float BETA = 3950.0f;

// Fan PWM levels
const uint8_t FAN12_IDLE_PWM = (uint8_t)(0.10 * 255);
const uint8_t FAN12_ACTIVE_PWM = (uint8_t)(0.55 * 255);

// Stepper parameters
volatile long encValue = 0;
int activeMotor = 0; // 0 = E0, 1 = E1
int lastClk = HIGH;

float speedE0_sps = 0;
float speedE1_sps = 0;

unsigned long nextStepE0_us = 0;
unsigned long nextStepE1_us = 0;
bool stepStateE0 = false;
bool stepStateE1 = false;

const float SPS_MIN = 0.0f;
const float SPS_MAX = 5000.0f;
const int   SPS_STEP = 50;

// State
unsigned long lastReadMs = 0;
bool heaterOn = false;
bool heatRequested = false;
bool relayFanLatched = false;
float lastTempC = 25;

// Debounce helpers
bool readButtonDebounced(uint8_t pin) {
  static unsigned long tLast = 0;
  static uint8_t last = HIGH, stable = HIGH;
  uint8_t r = digitalRead(pin);
  if (r != last) { tLast = millis(); last = r; }
  if (millis() - tLast > 25) stable = r;
  return (stable == LOW);
}

bool readEncBtnDebounced() {
  static unsigned long tLast = 0;
  static uint8_t last = HIGH, stable = HIGH;
  uint8_t r = digitalRead(ENC_SW);
  if (r != last) { tLast = millis(); last = r; }
  if (millis() - tLast > 25) stable = r;
  return (stable == LOW);
}

// Thermistor
float readThermistorC() {
  int a = analogRead(THERMISTOR_PIN);
  if (a <= 1)   return -9999.0f;
  if (a >= 1022) return 9999.0f;
  float Rt = R_SERIES * ((float)a / (1023.0f - (float)a));
  float invT = (1.0f / T0_K) + (1.0f / BETA) * log(Rt / R0);
  return (1.0f / invT) - 273.15f;
}

// Encoder
void pollEncoder() {
  int clkState = digitalRead(ENC_CLK);
  if (clkState != lastClk) {
    if (digitalRead(ENC_DT) != clkState) encValue++; else encValue--;
    if (activeMotor == 0)
      speedE0_sps = constrain(speedE0_sps + (encValue > 0 ? SPS_STEP : -SPS_STEP), SPS_MIN, SPS_MAX);
    else
      speedE1_sps = constrain(speedE1_sps + (encValue > 0 ? SPS_STEP : -SPS_STEP), SPS_MIN, SPS_MAX);
    encValue = 0;
  }
  lastClk = clkState;

  // toggle active motor
  static bool swLatch = false;
  bool pressed = readEncBtnDebounced();
  if (pressed && !swLatch) {
    swLatch = true;
    activeMotor = 1 - activeMotor;
    Serial.print("Active motor -> E"); Serial.println(activeMotor);
  } else if (!pressed) swLatch = false;
}

// Stepper control
void serviceSteppers() {
  unsigned long now = micros();

  // E0
  if (speedE0_sps > 0) {
    digitalWrite(A_ENABLE_PIN, LOW);
    unsigned long interval = (unsigned long)(1000000.0f / speedE0_sps / 2.0f);
    if (now >= nextStepE0_us) {
      stepStateE0 = !stepStateE0;
      digitalWrite(A_STEP_PIN, stepStateE0);
      nextStepE0_us = now + interval;
    }
  } else {
    digitalWrite(A_ENABLE_PIN, HIGH);
    digitalWrite(A_STEP_PIN, LOW);
  }

  // E1
  if (speedE1_sps > 0) {
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

// ====== Setup ======
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

  // Buttons / Encoder
  pinMode(HEAT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT,  INPUT_PULLUP);
  pinMode(ENC_SW,  INPUT_PULLUP);

  analogReference(DEFAULT);
  delay(50);
  lastReadMs = millis();

  Serial.println("System ready. Press HEAT button to toggle heating. Encoder SW toggles which motor to adjust (E0/E1).");
}

// ====== Loop ======
void loop() {
  pollEncoder();
  serviceSteppers();

  // Heat button toggle
  static bool heatBtnLatched = false;
  bool heatBtn = readButtonDebounced(HEAT_BUTTON_PIN);
  if (heatBtn && !heatBtnLatched) {
    heatBtnLatched = true;
    heatRequested = !heatRequested;
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
      Serial.println("TEMP FAULT: heater forced OFF");
    } else {
      if (heatRequested) {
        if (lastTempC < EXTRUDER_MIN_C) heaterOn = true;
        else if (lastTempC >= EXTRUDER_MAX_C) heaterOn = false;
      } else heaterOn = false;
    }

    digitalWrite(HEATER_PIN, heaterOn ? HIGH : LOW);

    // Relay FAN logic (formerly Relay2)
    if (heatRequested || heaterOn) relayFanLatched = true;
    if (!heatRequested && !heaterOn && lastTempC < COOL_RELAYFAN_OFF_C) relayFanLatched = false;
    digitalWrite(RELAY_FAN_PIN, relayFanLatched ? RELAY_ON : RELAY_OFF);

    // Relay VIBRATOR logic (formerly Relay1)
    bool anySpin = (speedE0_sps > 0.0f) || (speedE1_sps > 0.0f);
    digitalWrite(RELAY_VIBRATOR_PIN, anySpin ? RELAY_ON : RELAY_OFF);

    // Fans
    digitalWrite(FAN5V_1_PIN, heaterOn ? HIGH : LOW);
    digitalWrite(FAN5V_2_PIN, heaterOn ? HIGH : LOW);

    uint8_t pwm = (heaterOn || anySpin) ? FAN12_ACTIVE_PWM : FAN12_IDLE_PWM;
    analogWrite(FAN12_PWM_PIN, pwm);

    // Debug
    Serial.print("T="); Serial.print(lastTempC, 1);
    Serial.print(" | heatReq="); Serial.print(heatRequested);
    Serial.print(" | heaterOn="); Serial.print(heaterOn);
    Serial.print(" | E0="); Serial.print(speedE0_sps);
    Serial.print(" | E1="); Serial.print(speedE1_sps);
    Serial.print(" | Vibrator="); Serial.print(anySpin);
    Serial.print(" | FanRelay="); Serial.print(relayFanLatched);
    Serial.print(" | PWM="); Serial.println(pwm);
  }
}
