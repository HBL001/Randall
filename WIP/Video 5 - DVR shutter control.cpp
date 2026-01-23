/*
    DVR shutter control with status LED
    
    Toggles DVR_CCD (shutter gate drive enable) and a status LED
    on and off at regular intervals.
    
    Designed for easy observation with a multimeter or oscilloscope.

*/


#include <Arduino.h>

static constexpr uint8_t PIN_DVR_CCD   = 7;  // D7 -> DVR_CCD (gate drive enable)
static constexpr uint8_t PIN_STATUSLED = 6;  // D6 -> Status LED

// Meter-friendly timing
static constexpr uint32_t ON_MS  = 4000;  // 2.0 s ON
static constexpr uint32_t OFF_MS = 4000;  // 2.0 s OFF

static bool state = false;               // false=OFF, true=ON
static uint32_t lastToggleMs = 0;

void setOutputs(bool on)
{
  digitalWrite(PIN_DVR_CCD, on ? HIGH : LOW);
  digitalWrite(PIN_STATUSLED, on ? HIGH : LOW);

  Serial.print(millis());
  Serial.print(F(" ms : DVR_CCD="));
  Serial.print(on ? F("HIGH") : F("LOW"));
  Serial.print(F("  LED="));
  Serial.println(on ? F("ON") : F("OFF"));
}

void setup()
{
  pinMode(PIN_DVR_CCD, OUTPUT);
  pinMode(PIN_STATUSLED, OUTPUT);

  // Start in OFF state (safe default)
  digitalWrite(PIN_DVR_CCD, LOW);
  digitalWrite(PIN_STATUSLED, LOW);

  Serial.begin(115200);
  delay(200);
  Serial.println(F("DVR_CCD + Status LED toggle test"));

  lastToggleMs = millis();
  setOutputs(state);
}

void loop()
{
  const uint32_t now = millis();
  const uint32_t interval = state ? ON_MS : OFF_MS;

  if (now - lastToggleMs >= interval)
  {
    state = !state;
    lastToggleMs = now;
    setOutputs(state);
  }
}
