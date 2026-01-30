/*
  INT1 sanity test

  - D3 (INT1) rising edge triggers a visible flash on D6
  - Nothing else
*/

#include <Arduino.h>
#include <avr/interrupt.h>

static constexpr uint8_t PIN_STATUS = 3;  // D3 / INT1
static constexpr uint8_t PIN_LED    = 6;  // D6 test LED

// Flag set by ISR
volatile bool g_flashRequest = false;

// INT1 ISR
ISR(INT1_vect)
{
  g_flashRequest = true;   // keep ISR minimal
}

void setup()
{
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_STATUS, INPUT);   // change to INPUT_PULLUP if required

  digitalWrite(PIN_LED, LOW);

  Serial.begin(115200);
  delay(200);
  Serial.println(F("INT1 rising-edge LED flash test"));

  // Configure INT1 for RISING edge
  cli();
  EICRA &= ~(_BV(ISC11) | _BV(ISC10));
  EICRA |=  (_BV(ISC11) | _BV(ISC10));   // 11 = rising edge
  EIFR  |=  _BV(INTF1);                  // clear pending
  EIMSK |=  _BV(INT1);                   // enable INT1
  sei();
}

void loop()
{
  if (g_flashRequest)
  {
    g_flashRequest = false;

    // Visible LED pulse
    digitalWrite(PIN_LED, HIGH);
    delay(50);            // 50 ms = very obvious flash
    digitalWrite(PIN_LED, LOW);

    Serial.println(F("INT1 rising edge detected"));
  }
}
