#include <Arduino.h>
#include "pins.h"

void setup()
{
    // Initialise pins exactly as in real system
    pins_init();

    // HARD ASSERT: DVR button must be released
    DVR_BTN_RELEASE();

    // Optional debug
#if CFG_DEBUG_SERIAL
    Serial.begin(115200);
    Serial.println("SANITY TEST: DVR button forced RELEASE");
#endif
}

void loop()
{
    // Continuously enforce RELEASE in case of wiring or logic error
    DVR_BTN_RELEASE();
    delay(100);
}
