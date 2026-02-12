/*
  DVR status mirror (matches dvr_led module style)

  Hardware:
  - D3 (INT1)  = READ_DVR (LOW = DVR LED ON)
  - D6         = Test/status LED
  - D5         = Buzzer via 2N7002K
*/

#include <Arduino.h>

static constexpr uint8_t PIN_READ_DVR = 3;  // D3 / INT1
static constexpr uint8_t PIN_LED      = 6;  // Status LED
static constexpr uint8_t PIN_BUZZER   = 5;  // Buzzer MOSFET gate

static volatile uint8_t g_dirty = 0;

static void isr_dvr_change()
{
  g_dirty = 1;
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
  Serial.println(F("DVR status mirror running (attachInterrupt)"));

  attachInterrupt(digitalPinToInterrupt(PIN_READ_DVR), isr_dvr_change, CHANGE);
  g_dirty = 1; // force initial sync
}

void loop()
{
  if (!g_dirty) return;
  g_dirty = 0;

  // READ_DVR is inverted by NPN:
  // LOW  = DVR LED ON
  // HIGH = DVR LED OFF
  const bool dvrLedOn = (digitalRead(PIN_READ_DVR) == LOW);

  digitalWrite(PIN_LED,    dvrLedOn ? HIGH : LOW);
  digitalWrite(PIN_BUZZER, dvrLedOn ? HIGH : LOW);

  Serial.print(F("DVR LED "));
  Serial.println(dvrLedOn ? F("ON") : F("OFF"));
}
