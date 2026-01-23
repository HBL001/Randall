/*
  KILL# Pin Countdown Test

  This test code asserts the KILL# pin low after a countdown period,
  simulating a low battery condition that should power down the device.

  Note to self: only works with battery power, cannot turn off if connected to USB power (yes, tried it).

*/


#include <Arduino.h>

static constexpr uint8_t PIN_KILLN = 9;   // D9 -> KILL# (active-low)
static constexpr uint8_t PIN_LED   = 6;   // D6 -> test LED

static constexpr uint32_t COUNTDOWN_S = 10;

void setup()
{
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  // Keep KILL# deasserted initially
  pinMode(PIN_KILLN, OUTPUT);
  digitalWrite(PIN_KILLN, HIGH);

  Serial.begin(115200);
  delay(200);

  Serial.println(F("KILL# countdown test (battery-only recommended)"));
  Serial.println(F("Will assert KILL# LOW after 10 seconds."));
  Serial.println();
}

void loop()
{
  // 1 Hz blink while counting down
  for (int s = COUNTDOWN_S; s > 0; --s)
  {
    Serial.print(F("KILL in "));
    Serial.print(s);
    Serial.println(F(" s"));

    digitalWrite(PIN_LED, HIGH);
    delay(100);
    digitalWrite(PIN_LED, LOW);
    delay(900);
  }

  Serial.println(F("Asserting KILL# LOW now."));
  digitalWrite(PIN_KILLN, LOW);   // ACTIVE: request shutdown

  // Hold KILL# asserted indefinitely; if power removal works, MCU dies anyway.
  while (true)
  {
    // Optional: fast blink for the brief moment before power drops
    digitalWrite(PIN_LED, HIGH);
    delay(50);
    digitalWrite(PIN_LED, LOW);
    delay(200);
  }
}
