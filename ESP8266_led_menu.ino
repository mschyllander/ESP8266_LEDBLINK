/*
  dual_led controll (C)Mats Schyllander 2025
  Kontrollpanel via Serial för två LED på ESP8266:
   - LED1 på D2 (GPIO4)
   - LED2 på D1 (GPIO5)
   - Potentiometer på A0 kan styra ljusstyrka (PWM) på valfri LED

  Funktioner:
   - PÅ/AV (steady)
   - Blink med inställbar period (ms)
   - PWM-dimming (0..1023)
   - Potentiometerstyrning av LED1 eller LED2 (valbart)
   - Non-blocking (använder millis)
   - Statusvisning och meny via Serial
*/

#include <Arduino.h>

// ---------------- Pin-definitioner ----------------
const int LED1_PIN = D2;   // GPIO4, extern LED1
const int LED2_PIN = D1;   // GPIO5, extern LED2
const int POT_PIN  = A0;   // potentiometer på A0

// Externa LED:ar är active-high: HIGH = tänd, LOW = släckt
bool LED1_ACTIVE_LOW = false;
bool LED2_ACTIVE_LOW = false;

// ---------------- Tillståndstyper ----------------
enum LedMode { LM_OFF=0, LM_ON=1, LM_BLINK=2, LM_PWM=3 };

// potentiometer-target
enum PotTarget { PT_NONE=0, PT_LED1=1, PT_LED2=2 };
bool potEnabled = false;         // styr pot LED just nu?
PotTarget potTarget = PT_NONE;   // vilken LED potten styr

struct LedState {
  LedMode mode = LM_OFF;
  unsigned long blinkInterval = 500;
  unsigned long lastToggle = 0;
  bool currentOn = false;
  int pwmValue = 1023;   // -1 = ej PWM
  int fadeDirection = 1;
  unsigned long lastFade = 0;
  int fadeStep = 8;
};

LedState led1, led2;

// ---------------- Utgångshjälp ----------------
void applyOutput(int pin, bool activeLow, bool on, int pwmVal) {
  if (pwmVal >= 0) {
    int outVal = pwmVal;
    if (activeLow) outVal = 1023 - pwmVal;
    analogWrite(pin, outVal);
  } else {
    if (activeLow) {
      digitalWrite(pin, on ? LOW : HIGH);
    } else {
      digitalWrite(pin, on ? HIGH : LOW);
    }
  }
}

void setLedDigital(LedState &s, int pin, bool activeLow, bool on) {
  s.currentOn = on;
  s.pwmValue = -1;
  applyOutput(pin, activeLow, on, -1);
}

void setLedPwm(LedState &s, int pin, bool activeLow, int pwm) {
  s.mode = LM_PWM;
  s.pwmValue = constrain(pwm, 0, 1023);
  s.currentOn = (s.pwmValue > 0);
  applyOutput(pin, activeLow, s.currentOn, s.pwmValue);
}

// ---------------- Utskrift ----------------
void printMenu() {
  Serial.println();
  Serial.println("=== LED Kontrollmeny ===");
  Serial.println("1: LED1 PÅ  (steady)");
  Serial.println("2: LED1 AV  (steady)");
  Serial.println("3: LED1 Blink (ange ms interval)");
  Serial.println("4: LED1 PWM (ange 0..1023)");
  Serial.println();
  Serial.println("5: LED2 PÅ  (steady)");
  Serial.println("6: LED2 AV  (steady)");
  Serial.println("7: LED2 Blink (ange ms interval)");
  Serial.println("8: LED2 PWM (ange 0..1023)");
  Serial.println();
  Serial.println("9: Visa status");
  Serial.println("0: Visa meny");
  Serial.println("p: Slå PÅ/AV potentiometer-styrning");
  Serial.println("l: Välj vilken LED potten ska styra (1 eller 2)");
  Serial.println("q: Slå av båda LED");
  Serial.println("-------------------------");
}

void printStatus() {
  Serial.println();
  Serial.println("--- Status ---");
  Serial.print("LED1 (D2 / GPIO4): ");
  Serial.print( led1.mode == LM_OFF ? "OFF" : led1.mode == LM_ON ? "ON" : led1.mode == LM_BLINK ? "BLINK" : "PWM" );
  Serial.print(" | blink(ms): ");
  Serial.print(led1.blinkInterval);
  Serial.print(" | pwm: ");
  Serial.print(led1.pwmValue);
  Serial.print(" | curOn: ");
  Serial.println(led1.currentOn ? "YES" : "NO");

  Serial.print("LED2 (D1 / GPIO5): ");
  Serial.print( led2.mode == LM_OFF ? "OFF" : led2.mode == LM_ON ? "ON" : led2.mode == LM_BLINK ? "BLINK" : "PWM" );
  Serial.print(" | blink(ms): ");
  Serial.print(led2.blinkInterval);
  Serial.print(" | pwm: ");
  Serial.print(led2.pwmValue);
  Serial.print(" | curOn: ");
  Serial.println(led2.currentOn ? "YES" : "NO");

  Serial.print("Potentiometer: ");
  Serial.print(potEnabled ? "AKTIV" : "INAKTIV");
  Serial.print(" | styr: ");
  if (!potEnabled || potTarget == PT_NONE) Serial.println("ingen");
  else if (potTarget == PT_LED1) Serial.println("LED1");
  else if (potTarget == PT_LED2) Serial.println("LED2");

  Serial.println("---------------");
}

// ---------------- Inmatning ----------------
long readNumberFromSerial() {
  while (!Serial.available()) {
    delay(5);
  }
  String s = Serial.readStringUntil('\n');
  s.trim();
  return s.toInt();
}

// ---------------- Menyhantering ----------------
void handleMenuCommand(char c) {
  switch (c) {
    case '1': // LED1 ON
      led1.mode = LM_ON;
      setLedDigital(led1, LED1_PIN, LED1_ACTIVE_LOW, true);
      Serial.println("LED1 = ON");
      break;

    case '2': // LED1 OFF
      led1.mode = LM_OFF;
      setLedDigital(led1, LED1_PIN, LED1_ACTIVE_LOW, false);
      Serial.println("LED1 = OFF");
      break;

    case '3': { // LED1 blink
      Serial.println("Ange blink-interval i ms (ex 500): ");
      long v = readNumberFromSerial();
      if (v < 50) v = 50;
      led1.blinkInterval = v;
      led1.mode = LM_BLINK;
      led1.lastToggle = millis();
      led1.currentOn = false;
      setLedDigital(led1, LED1_PIN, LED1_ACTIVE_LOW, false);
      Serial.print("LED1 BLINK interval = ");
      Serial.print(v);
      Serial.println(" ms");
    } break;

    case '4': { // LED1 PWM
      Serial.println("Ange PWM 0..1023 (0 = av, 1023 = max): ");
      long v = readNumberFromSerial();
      if (v < 0) v = 0;
      if (v > 1023) v = 1023;
      led1.mode = LM_PWM;
      setLedPwm(led1, LED1_PIN, LED1_ACTIVE_LOW, v);
      Serial.print("LED1 PWM = ");
      Serial.println(v);
    } break;

    case '5': // LED2 ON
      led2.mode = LM_ON;
      setLedDigital(led2, LED2_PIN, LED2_ACTIVE_LOW, true);
      Serial.println("LED2 = ON");
      break;

    case '6': // LED2 OFF
      led2.mode = LM_OFF;
      setLedDigital(led2, LED2_PIN, LED2_ACTIVE_LOW, false);
      Serial.println("LED2 = OFF");
      break;

    case '7': { // LED2 blink
      Serial.println("Ange blink-interval i ms (ex 500): ");
      long v = readNumberFromSerial();
      if (v < 50) v = 50;
      led2.blinkInterval = v;
      led2.mode = LM_BLINK;
      led2.lastToggle = millis();
      led2.currentOn = false;
      setLedDigital(led2, LED2_PIN, LED2_ACTIVE_LOW, false);
      Serial.print("LED2 BLINK interval = ");
      Serial.print(v);
      Serial.println(" ms");
    } break;

    case '8': { // LED2 PWM
      Serial.println("Ange PWM 0..1023 (0 = av, 1023 = max): ");
      long v = readNumberFromSerial();
      if (v < 0) v = 0;
      if (v > 1023) v = 1023;
      led2.mode = LM_PWM;
      setLedPwm(led2, LED2_PIN, LED2_ACTIVE_LOW, v);
      Serial.print("LED2 PWM = ");
      Serial.println(v);
    } break;

    case '9':
      printStatus();
      break;

    case '0':
      printMenu();
      break;

    case 'p': // toggle potentiometer-styrning
      potEnabled = !potEnabled;
      Serial.print("Potentiometer-styrning: ");
      Serial.println(potEnabled ? "AKTIVERAD" : "AVSTÄNGD");
      break;

    case 'l': { // välj vilken LED potten ska styra
      Serial.println("Vilken LED ska potten styra? (1 eller 2, annat = ingen)");
      long v = readNumberFromSerial();
      if (v == 1) {
        potTarget = PT_LED1;
        Serial.println("Potten styr nu LED1.");
      } else if (v == 2) {
        potTarget = PT_LED2;
        Serial.println("Potten styr nu LED2.");
      } else {
        potTarget = PT_NONE;
        Serial.println("Potten styr nu ingen LED.");
      }
    } break;

    case 'q':
    case 'Q':
      led1.mode = LM_OFF;
      led2.mode = LM_OFF;
      setLedDigital(led1, LED1_PIN, LED1_ACTIVE_LOW, false);
      setLedDigital(led2, LED2_PIN, LED2_ACTIVE_LOW, false);
      Serial.println("Båda LED avstängda.");
      break;

    default:
      Serial.println("Okänt kommando. Tryck 0 för meny.");
      break;
  }
}

// ---------------- Uppdatering (icke-blockerande) ----------------
void updateLedState(LedState &s, int pin, bool activeLow) {
  unsigned long now = millis();

  if (s.mode == LM_BLINK) {
    if (now - s.lastToggle >= s.blinkInterval) {
      s.lastToggle = now;
      s.currentOn = !s.currentOn;
      setLedDigital(s, pin, activeLow, s.currentOn);
    }
  } else if (s.mode == LM_PWM) {
    applyOutput(pin, activeLow, s.currentOn, s.pwmValue);
  } else if (s.mode == LM_ON) {
    setLedDigital(s, pin, activeLow, true);
  } else { // LM_OFF
    setLedDigital(s, pin, activeLow, false);
  }
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("=== Dual LED kontroll med potentiometer ===");

  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);

  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);

  led1 = LedState();
  led2 = LedState();

  printMenu();
  Serial.print("Välj kommando: ");
}

String inputBuffer = "";

void loop() {
  // Läs seriell input
  if (Serial.available()) {
    char inChar = (char)Serial.read();
    if (inChar == '\r') {
      // ignorera CR
    } else if (inChar == '\n') {
      if (inputBuffer.length() > 0) {
        char cmd = inputBuffer.charAt(0);
        handleMenuCommand(cmd);
        inputBuffer = "";
        Serial.print("\nVälj kommando: ");
      }
    } else {
      inputBuffer += inChar;
      if (inputBuffer.length() == 1) {
        char maybe = inputBuffer.charAt(0);
        // snabbkommando för enkla kommandon (utan extra värde)
        if (maybe == '1' || maybe == '2' || maybe == '5' || maybe == '6' ||
            maybe == '9' || maybe == '0' || maybe == 'q' || maybe == 'Q' ||
            maybe == 'p') {
          handleMenuCommand(maybe);
          inputBuffer = "";
          Serial.print("\nVälj kommando: ");
        }
        // '3','4','7','8','l' kräver extra input → väntar på hel rad
      }
    }
  }

  // Potentiometer-styrning (om aktiv och target vald)
  if (potEnabled && potTarget != PT_NONE) {
    int potRaw = analogRead(POT_PIN);   // 0..1023
    if (potTarget == PT_LED1) {
      setLedPwm(led1, LED1_PIN, LED1_ACTIVE_LOW, potRaw);
    } else if (potTarget == PT_LED2) {
      setLedPwm(led2, LED2_PIN, LED2_ACTIVE_LOW, potRaw);
    }
  }

  // Uppdatera LED-logik
  updateLedState(led1, LED1_PIN, LED1_ACTIVE_LOW);
  updateLedState(led2, LED2_PIN, LED2_ACTIVE_LOW);

  delay(5);
}

