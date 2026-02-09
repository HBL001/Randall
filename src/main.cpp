#include "config.h"
#include "action_queue.h"
#include "executor.h"

void setup()
{
  Serial.begin(115200);
  pins_init();

  actionq_init();
  executor_init();

  action_t a1 { millis(), ACT_LED_PATTERN, LED_SLOW_BLINK, 0 };
  actionq_push(&a1);

  action_t a2 { millis(), ACT_BEEP, BEEP_DOUBLE, 0 };
  actionq_push(&a2);
}

void loop()
{
  executor_poll(millis());
}
