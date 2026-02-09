// pins.h
//
// Authoritative pin mapping per:
// - "Randall - User Story - Final - Frozen Feb,8,26"
// - "100 question checklist - final" (Pin assignments table)
// - "Mega system architecture - Final"
//
// Notes:
// - ISP pins PB3/PB4/PB5 and RESET must remain "clean".
// - DVR LED is sensed digitally (via NPN sniffer), so no ADC thresholding.
// - KILL# is terminal power cut via LTC2954: treat as irreversible.


#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
// Arduino pin numbers (portable across Nano / ATmega328P core)
// -----------------------------------------------------------------------------
#define PIN_LTC_INT_N         2   // PD2 / INT0  : LTC2954 INT# (interrupt in)
#define PIN_DVR_STAT          3   // PD3 / INT1  : DVR LED status sense (digital in)

#define PIN_BUZZER_OUT        5   // PD5 / OC0B  : buzzer / haptic enable (out, PWM-capable)
#define PIN_STATUS_LED        6   // PD6 / OC0A  : user status LED (out, PWM-capable)
#define PIN_DVR_BTN_CMD       7   // PD7         : drives PhotoMOS to emulate DVR button (out)

#define PIN_KILL_N_O          9   // PB1         : KILL# output to LTC2954 (out)

#define PIN_FUELGAUGE_ADC     A0  // PC0 / ADC0  : battery divider midpoint (ADC in)

// Optional debug UART (Arduino core uses these):
#define PIN_UART_RX           0   // PD0 (D0)
#define PIN_UART_TX           1   // PD1 (D1)

// -----------------------------------------------------------------------------
// Electrical / logic conventions
// -----------------------------------------------------------------------------

// KILL# naming: your docs call it KILL# / KILL_N.
// Assumption (typical): active-low assert cuts power.
// If your hardware is inverted, flip this.
#define KILL_ASSERT_LEVEL     LOW
#define KILL_DEASSERT_LEVEL   HIGH

// DVR button emulation via PhotoMOS: drive pin "active" to close relay LED.
// Confirm in schematic whether HIGH = press or LOW = press.
// Default safe assumption: HIGH asserts the PhotoMOS LED (press).
#define DVR_BTN_PRESS_LEVEL    HIGH
#define DVR_BTN_RELEASE_LEVEL  LOW

// Status LED: assume active HIGH (MCU sources/sinks per your LED wiring).
// Flip if your LED is wired to +5 and MCU sinks.
#define STATUS_LED_ON_LEVEL    HIGH
#define STATUS_LED_OFF_LEVEL   LOW

// Buzzer/haptic via low-side N-MOSFET (2N7002): gate HIGH = on.
#define BUZZER_ON_LEVEL        HIGH
#define BUZZER_OFF_LEVEL       LOW

// DVR status input polarity:
// Because you’re using an NPN sniffer/inverter stage, the logic level may be inverted.
// Start with this, then adjust after first scope/logic capture.
// If it’s inverted, swap these.
#define DVR_STAT_ACTIVE_LEVEL  HIGH
#define DVR_STAT_INACTIVE_LEVEL LOW

// LTC2954 INT# is active-low on most variants; treat as active-low unless confirmed otherwise.
#define LTC_INT_ASSERT_LEVEL   LOW
#define LTC_INT_DEASSERT_LEVEL HIGH

// -----------------------------------------------------------------------------
// Fast direct-port helpers (optional, but handy for tight edge timing)
// -----------------------------------------------------------------------------
#define DVR_STAT_READ()        (PIND & _BV(PD3))          // raw port read (non-boolean)
#define LTC_INT_READ()         (PIND & _BV(PD2))

#define STATUS_LED_ON()        do { digitalWrite(PIN_STATUS_LED, STATUS_LED_ON_LEVEL); } while (0)
#define STATUS_LED_OFF()       do { digitalWrite(PIN_STATUS_LED, STATUS_LED_OFF_LEVEL); } while (0)

#define BUZZER_ON()            do { digitalWrite(PIN_BUZZER_OUT, BUZZER_ON_LEVEL); } while (0)
#define BUZZER_OFF()           do { digitalWrite(PIN_BUZZER_OUT, BUZZER_OFF_LEVEL); } while (0)

#define DVR_BTN_PRESS()        do { digitalWrite(PIN_DVR_BTN_CMD, DVR_BTN_PRESS_LEVEL); } while (0)
#define DVR_BTN_RELEASE()      do { digitalWrite(PIN_DVR_BTN_CMD, DVR_BTN_RELEASE_LEVEL); } while (0)

#define KILL_ASSERT()          do { digitalWrite(PIN_KILL_N_O, KILL_ASSERT_LEVEL); } while (0)
#define KILL_DEASSERT()        do { digitalWrite(PIN_KILL_N_O, KILL_DEASSERT_LEVEL); } while (0)

// -----------------------------------------------------------------------------
// Centralised GPIO init (call once in setup())
// -----------------------------------------------------------------------------
static inline void pins_init(void)
{
  // Inputs
  pinMode(PIN_LTC_INT_N, INPUT_PULLUP);   // INT# typically active-low; pull-up gives known idle
  pinMode(PIN_DVR_STAT,  INPUT);          // external pull-up exists on board per design intent

  pinMode(PIN_FUELGAUGE_ADC, INPUT);      // ADC input

  // Outputs (default safe states first)
  pinMode(PIN_DVR_BTN_CMD, OUTPUT);
  DVR_BTN_RELEASE();

  pinMode(PIN_STATUS_LED, OUTPUT);
  STATUS_LED_OFF();

  pinMode(PIN_BUZZER_OUT, OUTPUT);
  BUZZER_OFF();

  pinMode(PIN_KILL_N_O, OUTPUT);
  KILL_DEASSERT(); // keep power alive until you intentionally cut it
}
