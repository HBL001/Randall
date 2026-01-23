/* Battery Gauge Test 

This demo shows the battery gauge functioning, designed to find the spot where the battery voltage drops below a certain threshold. 

Part of a battery protection scheme

With a bench PSU on +BAT rail:

Test	Expected result
8.4 V	LED solid ON, Serial ≈ 8.3–8.5 V
7.5 V	LED solid ON
7.0 V	Transition point
6.9 V	LED starts flashing
6.5 V	Flashing clearly



**
**  In a real test the threshold was 7.18V
** 


*/


#include <Arduino.h>

// -------- Pin assignments --------
static constexpr uint8_t PIN_FUELGAUGE = A0;
static constexpr uint8_t PIN_LED       = 6;   // D6

// -------- Divider values (ohms) --------
static constexpr float R_TOP    = 68000.0;  // R10
static constexpr float R_BOTTOM = 33000.0;  // R11

// -------- System constants --------
static constexpr float ADC_REF_V   = 5.0;    // Vref = AVcc
static constexpr float VBAT_THRESH = 7.0;    // volts

// -------- Timing --------
static constexpr uint32_t BLINK_MS = 500;

uint32_t lastBlink = 0;
bool ledState = false;

void setup()
{
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  Serial.begin(115200);
  delay(200);

  Serial.println(F("Fuel gauge threshold test"));
  Serial.println(F("LED solid ON  = VBAT >= 7.0 V"));
  Serial.println(F("LED flashing = VBAT <  7.0 V"));
  Serial.println();
}

void loop()
{
  // ---- Read ADC ----
  uint16_t adc = analogRead(PIN_FUELGAUGE);

  // ---- Convert to voltage ----
  float v_adc  = (adc * ADC_REF_V) / 1023.0;
  float v_bat  = v_adc * (R_TOP + R_BOTTOM) / R_BOTTOM;

  // ---- Report ----
  Serial.print(F("ADC="));
  Serial.print(adc);
  Serial.print(F("  VBAT="));
  Serial.print(v_bat, 2);
  Serial.println(F(" V"));

  // ---- LED logic ----
  if (v_bat >= VBAT_THRESH)
  {
    // Above threshold: solid ON
    digitalWrite(PIN_LED, HIGH);
  }
  else
  {
    // Below threshold: flash
    uint32_t now = millis();
    if (now - lastBlink >= BLINK_MS)
    {
      lastBlink = now;
      ledState = !ledState;
      digitalWrite(PIN_LED, ledState ? HIGH : LOW);
    }
  }

  delay(200);  // slow the serial spam
}


