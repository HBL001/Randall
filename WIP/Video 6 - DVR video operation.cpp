/*
    DVR Test video
    
    DVR power comes on with +BAT

    DVR shutter held down to Switch on
    DVR Shutter pushed to start recording
    A 10 second film please
    DVR Shutter pushed to stop recording
    DVR shutter held down to Switch on
    DVR power goes off with +BAT

    Designed for easy observation with a multimeter or oscilloscope.

*/

#include <Arduino.h>

static constexpr uint8_t PIN_LED     = 6;   // D6 test LED
static constexpr uint8_t PIN_SHUTTER = 7;   // D7 -> your shutter "press" output

// Timings (ms) - adjust here
static constexpr uint32_t T_START_UP      = 3000;   // safer than 250 ms
static constexpr uint32_t T_SHORT_PRESS   = 500;   // safer than 250 ms
static constexpr uint32_t T_BETWEEN       = 3000;
static constexpr uint32_t T_RECORD_WINDOW = 10000;
static constexpr uint32_t T_LONG_PRESS    = 3000;  // safer than 1 s

static void flashLedDuringPress(uint32_t press_ms)
{
  // LED on for the press duration, then off
  // Flash D6 to indicate shutter press

  digitalWrite(PIN_LED, HIGH);
  digitalWrite(PIN_SHUTTER, HIGH);
  delay(press_ms);
  digitalWrite(PIN_SHUTTER, LOW);
  digitalWrite(PIN_LED, LOW);
}

static void shortPress()
{
  Serial.println(F("Short press (start/stop toggle)"));
  flashLedDuringPress(T_SHORT_PRESS);
}

static void longPressPowerOff()
{
  Serial.println(F("Long press (power off)"));
  flashLedDuringPress(T_LONG_PRESS);
}

static void longPressPowerOn()
{
  Serial.println(F("Long press (power on)"));
  flashLedDuringPress(T_START_UP);
}

void setup()
{
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_SHUTTER, OUTPUT);

  digitalWrite(PIN_LED, LOW);
  digitalWrite(PIN_SHUTTER, LOW);

  Serial.begin(115200);
  delay(200);

  Serial.println(F("DVR shutter timing test sequence starting..."));

  // 1) Long press + LED, wait 2 s
  longPressPowerOn();
  delay(T_BETWEEN);

  // 2) Short press + LED, record 10 s
  shortPress();
  Serial.println(F("Recording window..."));
  delay(T_RECORD_WINDOW);

  // 3) Short press + LED, wait 2 s (stop recording)
  shortPress();
  delay(T_BETWEEN);

  // 4) Long press to shut down
  longPressPowerOff();

  Serial.println(F("Sequence complete."));
}

void loop()
{
  // Do nothing; sequence runs once.
}
