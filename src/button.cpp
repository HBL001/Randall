// button.cpp

#include "button.h"

#include <Arduino.h>

#include "pins.h"
#include "timings.h"
#include "event_queue.h"

// -----------------------------------------------------------------------------
// Internal state
// -----------------------------------------------------------------------------
static uint8_t  g_last_level             = HIGH;
static uint32_t g_last_edge_ms           = 0;

static bool     g_pressed                = false;
static uint32_t g_down_ms                = 0;

static bool     g_grace_long_emitted     = false;
static uint16_t g_last_press_ms          = 0;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static inline void emit(uint32_t now_ms,
                        event_id_t id,
                        event_source_t src,
                        event_reason_t reason,
                        uint16_t arg0,
                        uint16_t arg1)
{
    event_t e;
    e.t_ms   = now_ms;
    e.id     = id;
    e.src    = src;
    e.reason = reason;
    e.arg0   = arg0;
    e.arg1   = arg1;
    (void)eventq_push(&e);
}

static inline uint16_t clamp_u16(uint32_t v)
{
    return (v > 0xFFFFu) ? 0xFFFFu : (uint16_t)v;
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------
void button_init(void)
{
    // pins_init() is the authoritative pin setup; assume it's already called.
    g_last_level         = (uint8_t)digitalRead(PIN_LTC_INT_N);
    g_last_edge_ms       = millis();

    g_pressed            = (g_last_level == LTC_INT_ASSERT_LEVEL);
    g_down_ms            = g_pressed ? g_last_edge_ms : 0;

    g_grace_long_emitted = false;
    g_last_press_ms      = 0;
}

void button_poll(uint32_t now_ms)
{
    const uint8_t level = (uint8_t)digitalRead(PIN_LTC_INT_N);

    // -------------------------------------------------------------------------
    // Edge detect + debounce
    // -------------------------------------------------------------------------
    if (level != g_last_level)
    {
        // debounce window
        if ((uint32_t)(now_ms - g_last_edge_ms) >= (uint32_t)T_BTN_DEBOUNCE_MS)
        {
            g_last_edge_ms = now_ms;
            g_last_level   = level;

            // emit raw edge telemetry
            if (level == LTC_INT_ASSERT_LEVEL)
            {
                emit(now_ms, EV_LTC_INT_ASSERTED, SRC_LTC, EVR_EDGE_FALL, (uint16_t)level, 0);
            }
            else
            {
                emit(now_ms, EV_LTC_INT_DEASSERTED, SRC_LTC, EVR_EDGE_RISE, (uint16_t)level, 0);
            }

            // update press tracking
            if (level == LTC_INT_ASSERT_LEVEL)
            {
                g_pressed            = true;
                g_down_ms            = now_ms;
                g_grace_long_emitted = false;
            }
            else
            {
                // release: classify if we didn't already grace-emit
                if (g_pressed)
                {
                    const uint32_t press_ms_u32 = (uint32_t)(now_ms - g_down_ms);
                    const uint16_t press_ms     = clamp_u16(press_ms_u32);
                    g_last_press_ms             = press_ms;

                    if (!g_grace_long_emitted)
                    {
                        if (press_ms >= (uint16_t)T_BTN_SHORT_MIN_MS &&
                            press_ms <  (uint16_t)T_BTN_GRACE_MS)
                        {
                            emit(now_ms, EV_BTN_SHORT_PRESS, SRC_BUTTON, EVR_INTERNAL, press_ms, 0);
                        }
                        else if (press_ms >= (uint16_t)T_BTN_GRACE_MS)
                        {
                            emit(now_ms, EV_BTN_LONG_PRESS, SRC_BUTTON, EVR_INTERNAL, press_ms, 0);
                        }
                        else
                        {
                            // Too short: ignore (bounce/noise)
                        }
                    }
                }

                g_pressed            = false;
                g_down_ms            = 0;
                g_grace_long_emitted = false;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Grace-hold early emit (allows software shutdown before LTC nuclear)
    // -------------------------------------------------------------------------
    if (g_pressed && !g_grace_long_emitted)
    {
        const uint32_t held_ms = (uint32_t)(now_ms - g_down_ms);
        if (held_ms >= (uint32_t)T_BTN_GRACE_MS)
        {
            emit(now_ms, EV_BTN_LONG_PRESS, SRC_BUTTON, EVR_TIMEOUT, clamp_u16(held_ms), 0);
            g_grace_long_emitted = true;
        }

        // T_BTN_NUCLEAR_MS is hardware-enforced; we do not fight it here.
    }
}

bool button_is_pressed(void)
{
    return g_pressed;
}

uint16_t button_last_press_ms(void)
{
    return g_last_press_ms;
}
