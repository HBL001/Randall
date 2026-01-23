
/*  Video 2 - Power Management Test 


    User presses button and Nano boots up, runs this test code

    Connect this to USB monitor and see the button press count, each time INT# goes low 

    User still connected to and holds button for OFF.  This is overridden due to +5 net being held high by USB VBUS.

    User disconnects USB power, button still held for OFF.  Device powers down.

    
    LTC2954ITS8-1PBF interrupt test
  
    The LTC2954 is a pushbutton on/off controller with
    long press and short press detection. It has an open-drain
    interrupt output (INT#) that signals button presses.
  
    This sketch counts the number of button presses by
    monitoring the INT# pin via an external interrupt.
  
    Connect the LTC2954 INT# pin to Arduino digital pin 2 (INT0).
  
    http://www.linear.com/product/LTC2954

*/



#include <Arduino.h>

static constexpr uint8_t PIN_INTN = 2;   // D2 / INT0 (LTC INT#)

volatile uint32_t clickCount = 0;
volatile bool clickFlag = false;

void onIntFalling()
{
  clickCount++;
  clickFlag = true;
}

void setup()
{
  pinMode(PIN_INTN, INPUT_PULLUP);   // INT# is active-low
  Serial.begin(115200);
  delay(200);

  Serial.println(F("INT click test (counter)"));

  attachInterrupt(
    digitalPinToInterrupt(PIN_INTN),
    onIntFalling,
    FALLING
  );
}

void loop()
{
  if (clickFlag)
  {
    noInterrupts();
    uint32_t count = clickCount;
    clickFlag = false;
    interrupts();

    Serial.print(F("click #"));
    Serial.println(count);
  }
}
