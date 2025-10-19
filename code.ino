// ----- RAMPS E0 stepper pins (kept, but motor won't spin) -----
#define A_STEP_PIN    54   // E0 STEP
#define A_DIR_PIN     55   // E0 DIR
#define A_ENABLE_PIN  38   // E0 ENABLE

// ----- Heater & sensor -----
#define HEATER_PIN     10      // D10 = E0 heater MOSFET gate on RAMPS
#define THERMISTOR_PIN A13     // RAMPS T0 (your probe -> GND + A13)

// ----- Fans (must drive via MOSFET/transistor!) -----
#define FAN1_PIN 4            // your 5V fan #1 (via small MOSFET)
#define FAN2_PIN 5            // your 5V fan #2 (via small MOSFET)
#define FAN12_PWM_PIN 9       // RAMPS D9 12V fan MOSFET (PWM-capable)

// ----- Temp window (edit these) -----
float EXTRUDER_MIN_C = 250.0f;   // start heating below this
float EXTRUDER_MAX_C = 280.0f;   // stop heating at/above this

// ----- Thermistor model (100k NTC, B=3950, 4.7k pull-up on RAMPS) -----
const float R_SERIES   = 4700.0f;
const float R0         = 100000.0f;
const float T0_K       = 273.15f + 25.0f;
const float BETA       = 3950.0f;

// ----- Safety limits -----
const float MAX_SAFE_C = 300.0f;
const unsigned long READ_INTERVAL_MS = 200;

// ----- Control flags -----
const bool RUN_STEPPER_TEST = false;

// housekeeping
unsigned long lastReadMs = 0;
bool heaterOn = false;

// --- helper: read thermistor (Celsius) using Beta model ---
float readThermistorC() {
  int a = analogRead(THERMISTOR_PIN);
  if (a <= 1)   return -9999.0f;
  if (a >= 1022) return 9999.0f;
  float Rt = R_SERIES * ( (float)a / (1023.0f - (float)a) );
  float invT = (1.0f / T0_K) + (1.0f / BETA) * log(Rt / R0);
  float Tk = 1.0f / invT;
  return Tk - 273.15f;
}

// --- optional stepper test (kept, not run) ---
void stepperSpinTest() {
  digitalWrite(A_DIR_PIN, HIGH);
  for (int i = 0; i < 80000; i++) {
    digitalWrite(A_STEP_PIN, HIGH); delayMicroseconds(20);
    digitalWrite(A_STEP_PIN, LOW);  delayMicroseconds(20);
  }
  delay(1000);
  digitalWrite(A_DIR_PIN, LOW);
  for (int i = 0; i < 2000; i++) {
    digitalWrite(A_STEP_PIN, HIGH); delayMicroseconds(500);
    digitalWrite(A_STEP_PIN, LOW);  delayMicroseconds(500);
  }
  delay(1000);
}

void setup() {
  // Stepper pins (kept)
  pinMode(A_STEP_PIN,   OUTPUT);
  pinMode(A_DIR_PIN,    OUTPUT);
  pinMode(A_ENABLE_PIN, OUTPUT);
  digitalWrite(A_ENABLE_PIN, HIGH);   // disable driver

  // Heater & fans
  pinMode(HEATER_PIN, OUTPUT);
  digitalWrite(HEATER_PIN, LOW);

  pinMode(FAN1_PIN, OUTPUT);
  pinMode(FAN2_PIN, OUTPUT);
  digitalWrite(FAN1_PIN, LOW);
  digitalWrite(FAN2_PIN, LOW);

  pinMode(FAN12_PWM_PIN, OUTPUT);
  analogWrite(FAN12_PWM_PIN, 0);      // off at start

  // Thermistor
  analogReference(DEFAULT);
  delay(50);
  lastReadMs = millis();

  Serial.begin(115200);
  Serial.println("Extruder heater + fans test ready.");
}

void loop() {
  if (RUN_STEPPER_TEST) {
    digitalWrite(A_ENABLE_PIN, LOW);
    stepperSpinTest();
  } else {
    digitalWrite(A_ENABLE_PIN, HIGH);
  }

  unsigned long now = millis();
  if (now - lastReadMs >= READ_INTERVAL_MS) {
    lastReadMs = now;

    float tc = readThermistorC();

    if (tc > MAX_SAFE_C || tc == 9999.0f || tc == -9999.0f) {
      heaterOn = false;
    } else {
      if (tc < EXTRUDER_MIN_C)       heaterOn = true;
      else if (tc >= EXTRUDER_MAX_C) heaterOn = false;
    }

    // apply outputs
    digitalWrite(HEATER_PIN, heaterOn ? HIGH : LOW);
    digitalWrite(FAN1_PIN,   heaterOn ? HIGH : LOW);
    digitalWrite(FAN2_PIN,   heaterOn ? HIGH : LOW);

    // 12V fan on D9 at ~50% whenever heating (128/255 ≈ 50%)
    analogWrite(FAN12_PWM_PIN, heaterOn ? 128 : 0);

    Serial.print("T(C)="); Serial.print(tc);
    Serial.print("  Heater="); Serial.print(heaterOn ? "ON" : "OFF");
    Serial.print("  D9 PWM="); Serial.println(heaterOn ? 128 : 0);
  }
  delay(2);
}
