#include <Arduino.h>

static constexpr uint8_t PIN_INTN = 2;   // D2 / INT0
static constexpr uint8_t PIN_LED  = 13;  // onboard LED

volatile uint32_t g_intCount = 0;

void onIntFalling() {
  g_intCount++;
}

void setup() {
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  pinMode(PIN_INTN, INPUT_PULLUP);
  Serial.begin(115200);

  attachInterrupt(digitalPinToInterrupt(PIN_INTN), onIntFalling, FALLING);

  Serial.println(F("\n--- INT# test ---"));
  Serial.println(F("Expect D2 HIGH normally; pulses LOW increments counter."));
}

void loop() {
  static uint32_t last = 0;
  static uint32_t lastPrint = 0;

  noInterrupts();
  uint32_t c = g_intCount;
  interrupts();

  if (c != last) {
    last = c;
    digitalWrite(PIN_LED, HIGH);
    delay(20);
    digitalWrite(PIN_LED, LOW);

    Serial.print(F("INT fired. Count="));
    Serial.println(c);
  }

  if (millis() - lastPrint > 2000) {
    lastPrint = millis();
    Serial.print(F("Heartbeat. D2="));
    Serial.print(digitalRead(PIN_INTN) ? "HIGH" : "LOW");
    Serial.print(F(" Count="));
    Serial.println(c);
  }
}
