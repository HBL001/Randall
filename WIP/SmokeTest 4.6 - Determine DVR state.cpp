/*
  main.cpp — DVR LED module verification harness (Randall)

  Purpose:
    - Exercise the production dvr_led module in isolation
    - Verify interrupt capture + classification using your canonical timings.h thresholds
    - Mirror instantaneous DVR LED phase on the status LED (so you can eyeball it)
    - Print pattern transitions and (optionally) raw phase transitions

  Uses:
    pins.h       -> pins_init(), PIN_DVR_STAT, PIN_STATUS_LED (if defined)
    timings.h    -> thresholds used inside dvr_led.cpp
    enums.h      -> dvr_led_pattern_t values
    dvr_led.h    -> dvr_led_init(), dvr_led_poll(), dvr_led_get_pattern()

  Notes:
    - This harness does NOT define any ISR directly (no ISR(INT1_vect)).
      It relies on dvr_led.cpp using attachInterrupt() on PIN_DVR_STAT.
    - If your pins.h does not expose a status LED pin, set PIN_TEST_LED below.
*/

#include <Arduino.h>

#include "config.h"
#include "pins.h"
#include "timings.h"
#include "enums.h"
#include "dvr_led.h"

// ----------------------------------------------------------------------------
// Select a test LED pin
// ----------------------------------------------------------------------------
// Prefer your canonical status LED pin if it exists; otherwise change PIN_TEST_LED.
#ifndef PIN_STATUS_LED
static constexpr uint8_t PIN_TEST_LED = 6;   // Nano D6 (adjust if needed)
#else
static constexpr uint8_t PIN_TEST_LED = PIN_STATUS_LED;
#endif

// ----------------------------------------------------------------------------
// Printing helpers
// ----------------------------------------------------------------------------
static dvr_led_pattern_t s_last_pat = DVR_LED_UNKNOWN;
static bool s_last_phase_on = false;

static void print_pat(dvr_led_pattern_t p)
{
#if CFG_DEBUG_SERIAL
    Serial.print(F("PATTERN -> "));
    switch (p)
    {
        case DVR_LED_OFF:           Serial.println(F("OFF")); break;
        case DVR_LED_SOLID:         Serial.println(F("SOLID")); break;
        case DVR_LED_SLOW_BLINK:    Serial.println(F("SLOW_BLINK")); break;
        case DVR_LED_FAST_BLINK:    Serial.println(F("FAST_BLINK")); break;
        case DVR_LED_ABNORMAL_BOOT: Serial.println(F("ABNORMAL_BOOT")); break;
        case DVR_LED_UNKNOWN:
        default:                    Serial.println(F("UNKNOWN")); break;
    }
#else
    (void)p;
#endif
}

// Optional raw phase print (set to 1 while debugging wiring)
#define PRINT_RAW_PHASE 0

static void poll_and_report(uint32_t now_ms)
{
    // Keep module running
    dvr_led_poll(now_ms);

    // Read current classification
    const dvr_led_pattern_t p = dvr_led_get_pattern();
    if (p != s_last_pat)
    {
        s_last_pat = p;
        print_pat(p);
    }

    // Mirror instantaneous phase for sanity: LOW=ON
    const bool phase_on = (digitalRead(PIN_DVR_STAT) == LOW);
    digitalWrite(PIN_TEST_LED, phase_on ? HIGH : LOW);

#if PRINT_RAW_PHASE && CFG_DEBUG_SERIAL
    if (phase_on != s_last_phase_on)
    {
        s_last_phase_on = phase_on;
        Serial.println(phase_on ? F("DVR LED ON") : F("DVR LED OFF"));
    }
#endif
}

void setup()
{
#if CFG_DEBUG_SERIAL
    Serial.begin(115200);
    delay(200);
    Serial.println(F("DVR LED module test harness starting"));
#endif

    // Your project init
    pins_init();

    // Local LED output
    pinMode(PIN_TEST_LED, OUTPUT);
    digitalWrite(PIN_TEST_LED, LOW);

    // Ensure the DVR status input is in the right mode (dvr_led_init sets it too)
    pinMode(PIN_DVR_STAT, INPUT);

    // Init the DVR LED module (it attaches interrupt internally)
    dvr_led_init();

#if CFG_DEBUG_SERIAL
    Serial.println(F("Expectations:"));
    Serial.print(F("  - SOLID when DVR idle-on (quiet >= ")); Serial.print(T_SOLID_MS); Serial.println(F(" ms)"));
    Serial.print(F("  - SLOW_BLINK when recording (period ")); Serial.print(T_SLOW_MIN_MS); Serial.print(F("..")); Serial.print(T_SLOW_MAX_MS); Serial.println(F(" ms)"));
    Serial.print(F("  - FAST_BLINK during shutdown/error burst (period ")); Serial.print(T_FAST_MIN_MS); Serial.print(F("..")); Serial.print(T_FAST_MAX_MS); Serial.println(F(" ms)"));
    Serial.println(F("Running..."));
#endif
}

void loop()
{
    const uint32_t now = millis();
    poll_and_report(now);

    // Non-blocking: keep loop tight so we don't starve polling.
    // (Interrupt capture is primary; polling just drains and classifies.)
}
