// UPDATE: add GRACE hold handling (power-off via long shutter press)
// Policy:
// - SHORT press: [T_BTN_SHORT_MIN_MS .. T_BTN_GRACE_MS) => power-on if needed else toggle record
// - GRACE hold:  [T_BTN_GRACE_MS .. T_BTN_NUCLEAR_MS)   => power-off (long shutter) if powered
// - NUCLEAR:     >= T_BTN_NUCLEAR_MS                   => ignore in software (hardware LTC wins)
//   SMOKE: short->power-on/toggle, grace-hold->power-off, nuclear ignored PRESS ms=283 Action: DVR POWER ON (LONG press) DVR presumed ready after wait. 
//   PRESS ms=251 Action: DVR TOGGLE (SHORT press) 
//   PRESS ms=244 Action: DVR TOGGLE (SHORT press) 
//   PRESS ms=1078 Action: DVR POWER OFF (LONG press) [grace hold] DVR presumed ready after wait.
//
//   The DVR should follow this sequence:
//   1) Power on (long press), wait ~2.5 s, then be ready to record
//   2) Short press to start recording, short press to stop
//   3) Long press to power off (grace hold)
//


#include "config.h"
#include "pins.h"
#include "timings.h"
#include "event_queue.h"
#include "action_queue.h"
#include "executor.h"

static volatile uint8_t s_last_int_level = 1;

// Button tracking
static bool     g_btn_down = false;
static uint32_t g_down_ms  = 0;

// DVR “assumed powered” latch (until LED classifier exists)
static bool     g_dvr_powered = false;

// Simple sequencer for “power-on then maybe wait”
typedef enum
{
    SEQ_IDLE = 0,
    SEQ_PWRON_WAIT
} seq_state_t;

static seq_state_t s_seq = SEQ_IDLE;
static uint32_t    s_deadline_ms = 0;

static inline bool time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static inline void enqueue_beep(uint32_t now, beep_pattern_t pat)
{
    action_t a;
    a.t_enq_ms = now;
    a.id       = ACT_BEEP;
    a.arg0     = (uint16_t)pat;
    a.arg1     = 0;
    actionq_push(&a);
}

static inline void enqueue_dvr_short(uint32_t now)
{
    action_t a;
    a.t_enq_ms = now;
    a.id       = ACT_DVR_PRESS_SHORT;
    a.arg0     = 0;
    a.arg1     = 0;
    actionq_push(&a);
}

static inline void enqueue_dvr_long(uint32_t now)
{
    action_t a;
    a.t_enq_ms = now;
    a.id       = ACT_DVR_PRESS_LONG;
    a.arg0     = 0;
    a.arg1     = 0;
    actionq_push(&a);
}

// INT# edge ISR -> event queue
static void isr_ltc_int_change()
{
    const uint8_t level = (uint8_t)digitalRead(PIN_LTC_INT_N);
    if (level == s_last_int_level) return;
    s_last_int_level = level;

    event_t e;
    e.t_ms   = millis();
    e.src    = SRC_LTC;
    e.reason = (level == LTC_INT_ASSERT_LEVEL) ? EVR_EDGE_FALL : EVR_EDGE_RISE;
    e.arg0   = (uint16_t)level;
    e.arg1   = 0;
    e.id     = (level == LTC_INT_ASSERT_LEVEL) ? EV_LTC_INT_ASSERTED : EV_LTC_INT_DEASSERTED;

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

    s_last_int_level = (uint8_t)digitalRead(PIN_LTC_INT_N);
    attachInterrupt(digitalPinToInterrupt(PIN_LTC_INT_N), isr_ltc_int_change, CHANGE);

#if CFG_DEBUG_SERIAL
    Serial.println("SMOKE: short->power-on/toggle, grace-hold->power-off, nuclear ignored");
#endif
}

void loop()
{
    const uint32_t now = millis();

    // Always run executor
    executor_poll(now);

    // Sequencer: after power-on/off long press, wait a bit before allowing next actions
    if (s_seq == SEQ_PWRON_WAIT)
    {
        if (!time_reached(now, s_deadline_ms))
            return;

        s_seq = SEQ_IDLE;

#if CFG_DEBUG_SERIAL
        Serial.println("DVR presumed ready after wait.");
#endif
    }

    // Drain edge events and classify press on release
    event_t ev;
    while (eventq_pop(&ev))
    {
        if (ev.id == EV_LTC_INT_ASSERTED)
        {
            g_btn_down = true;
            g_down_ms  = ev.t_ms;
        }
        else if (ev.id == EV_LTC_INT_DEASSERTED && g_btn_down)
        {
            const uint32_t press_ms = ev.t_ms - g_down_ms;
            g_btn_down = false;
            g_down_ms  = 0;

#if CFG_DEBUG_SERIAL
            Serial.print("PRESS ms="); Serial.println(press_ms);
#endif

            // -----------------------------------------------------------------
            // 1) SHORT press band: [SHORT_MIN .. GRACE)
            // -----------------------------------------------------------------
            if (press_ms >= T_BTN_SHORT_MIN_MS && press_ms < T_BTN_GRACE_MS)
            {
                if (executor_busy())
                    continue;

                if (!g_dvr_powered)
                {
#if CFG_DEBUG_SERIAL
                    Serial.println("Action: DVR POWER ON (LONG press)");
#endif
                    enqueue_dvr_long(now);
                    enqueue_beep(now, BEEP_DOUBLE);

                    g_dvr_powered = true;

                    // wait before allowing subsequent commands
                    s_seq = SEQ_PWRON_WAIT;
                    s_deadline_ms = now + T_DVR_AFTER_PWRON_MS;
                }
                else
                {
#if CFG_DEBUG_SERIAL
                    Serial.println("Action: DVR TOGGLE (SHORT press)");
#endif
                    enqueue_dvr_short(now);
                    enqueue_beep(now, BEEP_SINGLE);
                }
            }
            // -----------------------------------------------------------------
            // 2) GRACE hold band: [GRACE .. NUCLEAR)
            // -----------------------------------------------------------------
            else if (press_ms >= T_BTN_GRACE_MS && press_ms < T_BTN_NUCLEAR_MS)
            {
                if (executor_busy())
                    continue;

                if (g_dvr_powered)
                {
#if CFG_DEBUG_SERIAL
                    Serial.println("Action: DVR POWER OFF (LONG press) [grace hold]");
#endif
                    enqueue_dvr_long(now);
                    enqueue_beep(now, BEEP_DOUBLE);

                    g_dvr_powered = false;

                    // optional: small settle time after power-off
                    s_seq = SEQ_PWRON_WAIT;
                    s_deadline_ms = now + 500;
                }
                else
                {
#if CFG_DEBUG_SERIAL
                    Serial.println("IGN: grace hold while DVR not powered");
#endif
                }
            }
            // -----------------------------------------------------------------
            // 3) NUCLEAR (hardware wins): >= NUCLEAR
            // -----------------------------------------------------------------
            else
            {
#if CFG_DEBUG_SERIAL
                if (press_ms >= T_BTN_NUCLEAR_MS)
                    Serial.println("IGN: nuclear hold (LTC hardware will cut power)");
#endif
            }
        }
    }
}
