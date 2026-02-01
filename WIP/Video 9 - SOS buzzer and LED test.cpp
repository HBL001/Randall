/*
  SOS Morse Code Test
  ------------------
  Buzzer + Status LED in lockstep
*/

#include <Arduino.h>

static constexpr uint8_t PIN_BUZZER = 5;   // BUZZER net
static constexpr uint8_t PIN_LED    = 6;   // STATUS LED pin
static constexpr uint16_t T = 150;         // Morse unit (ms)

// --- primitives ---
inline void signalOn() {
  digitalWrite(PIN_BUZZER, HIGH);
  digitalWrite(PIN_LED, HIGH);
}

inline void signalOff() {
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED, LOW);
}

void dot() {
  signalOn();
  delay(T);
  signalOff();
  delay(T);
}

void dash() {
  signalOn();
  delay(3 * T);
  signalOff();
  delay(T);
}

void letterGap() {
  delay(2 * T); // already waited 1T after symbol → total 3T
}

void wordGap() {
  delay(6 * T); // already waited 1T → total 7T
}

void setup() {
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED, OUTPUT);

  signalOff();
}

void loop() {
  // S: · · ·
  dot(); dot(); dot();
  letterGap();

  // O: – – –
  dash(); dash(); dash();
  letterGap();

  // S: · · ·
  dot(); dot(); dot();

  wordGap();
}
