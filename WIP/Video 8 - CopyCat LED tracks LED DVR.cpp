/*
  DVR status mirror

  Hardware:
  - D3 (INT1)  = READ_DVR (LOW = DVR LED ON)
  - D6         = Test/status LED
  - D5         = Buzzer via 2N7002K

  Behaviour:
  - Status LED mirrors DVR LED state
  - Buzzer follows DVR LED ON state
*/

#include <Arduino.h>
#include <avr/interrupt.h>

static constexpr uint8_t PIN_READ_DVR = 3;  // D3 / INT1
static constexpr uint8_t PIN_LED      = 6;  // Status LED
static constexpr uint8_t PIN_BUZZER   = 5;  // Buzzer MOSFET gate

volatile uint8_t g_update = 0;

// INT1 ISR: flag only
ISR(INT1_vect)
{
  g_update = 1;
}

void setup()
{
  pinMode(PIN_READ_DVR, INPUT);   // external 10k pull-up present
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  digitalWrite(PIN_LED, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  Serial.begin(115200);
  delay(200);
  Serial.println(F("DVR status mirror running"));

  // INT1 on CHANGE (both edges)
  cli();
  EICRA &= ~(_BV(ISC11) | _BV(ISC10));
  EICRA |=  (_BV(ISC10));         // 01 = any logical change
  EIFR  |=  _BV(INTF1);           // clear pending
  EIMSK |=  _BV(INT1);            // enable INT1
  sei();

  g_update = 1;                   // force initial sync
}

void loop()
{
  if (!g_update)
    return;

  cli();
  g_update = 0;
  sei();

  // READ_DVR is inverted by NPN:
  // LOW  = DVR LED ON
  // HIGH = DVR LED OFF
  bool dvrLedOn = (digitalRead(PIN_READ_DVR) == LOW);

  digitalWrite(PIN_LED,    dvrLedOn ? HIGH : LOW);
  digitalWrite(PIN_BUZZER, dvrLedOn ? HIGH : LOW);

  Serial.print(F("DVR LED "));
  Serial.println(dvrLedOn ? F("ON") : F("OFF"));
}
