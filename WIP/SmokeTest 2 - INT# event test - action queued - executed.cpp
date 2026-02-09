/*SMOKE: LTC INT# -> event -> BEEP_DOUBLE (+ dt + press length)
EV id=1 level=0 t=7316
ASSERT dt_ms=7316
EV id=2 level=1 t=7473
PRESS ms=157
EV id=1 level=0 t=61537
ASSERT dt_ms=54221
EV id=2 level=1 t=61745
PRESS ms=208
EV id=1 level=0 t=85782
ASSERT dt_ms=24245
EV id=2 level=1 t=86889
PRESS ms=1107
*/


#include "config.h"
#include "event_queue.h"
#include "action_queue.h"
#include "executor.h"

static volatile uint8_t s_last_int_level = 1; // 1=deasserted (pull-up)

// Timing capture (main-context reads; ISR writes only level + event)
static uint32_t g_last_assert_ms = 0;   // for dt between asserts
static uint32_t g_last_down_ms   = 0;   // for press length

static void isr_ltc_int_change()
{
    const uint8_t level = (uint8_t)digitalRead(PIN_LTC_INT_N); // HIGH or LOW

    // Only enqueue when the level actually changes (belt + braces)
    if (level == s_last_int_level)
        return;

    s_last_int_level = level;

    event_t e;
    e.t_ms   = millis(); // OK for smoke test; replace with tick counter later
    e.src    = SRC_LTC;
    e.reason = (level == LTC_INT_ASSERT_LEVEL) ? EVR_EDGE_FALL : EVR_EDGE_RISE;
    e.arg0   = (uint16_t)level;
    e.arg1   = 0;

    e.id = (level == LTC_INT_ASSERT_LEVEL) ? EV_LTC_INT_ASSERTED : EV_LTC_INT_DEASSERTED;

    eventq_push_isr(&e);
}

void setup()
{
#if CFG_DEBUG_SERIAL
    Serial.begin(115200);
#endif

    pins_init();

    eventq_init();
    actionq_init();
    executor_init();

    // Prime last level + dt baseline (avoid "time since boot" on first press)
    s_last_int_level   = (uint8_t)digitalRead(PIN_LTC_INT_N);
    g_last_assert_ms   = millis();
    g_last_down_ms     = 0;

    // Attach INT0 (D2) change interrupt
    attachInterrupt(digitalPinToInterrupt(PIN_LTC_INT_N), isr_ltc_int_change, CHANGE);

#if CFG_DEBUG_SERIAL
    Serial.println("SMOKE: LTC INT# -> event -> BEEP_DOUBLE (+ dt + press length)");
#endif
}

void loop()
{
    const uint32_t now = millis();

    // Drain events and convert the ones we care about into actions
    event_t ev;
    while (eventq_pop(&ev))
    {
#if CFG_DEBUG_SERIAL
        Serial.print("EV id="); Serial.print((uint8_t)ev.id);
        Serial.print(" level="); Serial.print(ev.arg0);
        Serial.print(" t="); Serial.println(ev.t_ms);
#endif

        if (ev.id == EV_LTC_INT_ASSERTED)
        {
#if CFG_DEBUG_SERIAL
            uint32_t dt = ev.t_ms - g_last_assert_ms;   // wrap-safe
            g_last_assert_ms = ev.t_ms;
            g_last_down_ms   = ev.t_ms;
            Serial.print("ASSERT dt_ms="); Serial.println(dt);
#endif
            // Beep-beep on assert
            action_t a;
            a.t_enq_ms = now;
            a.id       = ACT_BEEP;
            a.arg0     = (uint16_t)BEEP_DOUBLE;
            a.arg1     = 0;
            actionq_push(&a);
        }
        else if (ev.id == EV_LTC_INT_DEASSERTED)
        {
#if CFG_DEBUG_SERIAL
            if (g_last_down_ms != 0)
            {
                uint32_t press_ms = ev.t_ms - g_last_down_ms; // wrap-safe
                Serial.print("PRESS ms="); Serial.println(press_ms);
                g_last_down_ms = 0;
            }
#endif
        }
    }

    // Always advance executor (runs beep + led engines)
    executor_poll(now);
}
