// dvr_button.cpp
//
// Button driver for LTC2954 INT#-qualified button input.
// - Polling-based edge detect + debounce (NO ISR ownership)
// - Emits gesture events:
//      EV_BTN_SHORT_PRESS on release if duration within [T_BTN_SHORT_MIN_MS .. T_BTN_GRACE_MS)
//      EV_BTN_LONG_PRESS  once when held reaches T_BTN_GRACE_MS (early emit),
//                         OR on release if held >= T_BTN_GRACE_MS and not yet emitted
// - Optional raw edge telemetry (EV_LTC_INT_ASSERTED / EV_LTC_INT_DEASSERTED)
//
// Dependencies: pins.h, timings.h, enums.h, event_queue.h

#include <Arduino.h>

#include "dvr_button.h"
#include "pins.h"
#include "timings.h"
#include "enums.h"
#include "event_queue.h"

// -----------------------------------------------------------------------------
// Optional debug telemetry
// -----------------------------------------------------------------------------
#ifndef CFG_BUTTON_EMIT_RAW_EDGES
#define CFG_BUTTON_EMIT_RAW_EDGES 0
#endif

// -----------------------------------------------------------------------------
// Internal state
// -----------------------------------------------------------------------------
static uint8_t  g_last_level         = HIGH;
static uint32_t g_last_edge_ms       = 0;

static bool     g_pressed            = false;
static uint32_t g_down_ms            = 0;

static bool     g_long_emitted       = false;  // emitted EV_BTN_LONG_PRESS for this press instance?
static uint16_t g_last_press_ms      = 0;

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

static inline bool is_asserted(uint8_t level)
{
    return (level == (uint8_t)LTC_INT_ASSERT_LEVEL);
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------
void button_init(void)
{
    // pins_init() is authoritative; assume already called.
    const uint32_t now_ms = millis();

    g_last_level    = (uint8_t)digitalRead(PIN_LTC_INT_N);
    g_last_edge_ms  = now_ms;

    g_pressed       = is_asserted(g_last_level);
    g_down_ms       = g_pressed ? now_ms : 0;

    g_long_emitted  = false;
    g_last_press_ms = 0;
}

void button_poll(uint32_t now_ms)
{
    const uint8_t level = (uint8_t)digitalRead(PIN_LTC_INT_N);

    // -------------------------------------------------------------------------
    // Edge detect + debounce
    // -------------------------------------------------------------------------
    if (level != g_last_level)
    {
        if ((uint32_t)(now_ms - g_last_edge_ms) >= (uint32_t)T_BTN_DEBOUNCE_MS)
        {
            g_last_edge_ms = now_ms;
            g_last_level   = level;

#if (CFG_BUTTON_EMIT_RAW_EDGES != 0)
            // Raw edge telemetry (debug only)
            if (is_asserted(level))
                emit(now_ms, EV_LTC_INT_ASSERTED, SRC_LTC, EVR_EDGE_FALL, (uint16_t)level, 0);
            else
                emit(now_ms, EV_LTC_INT_DEASSERTED, SRC_LTC, EVR_EDGE_RISE, (uint16_t)level, 0);
#endif

            // Press tracking
            if (is_asserted(level))
            {
                // Press down
                g_pressed      = true;
                g_down_ms      = now_ms;
                g_long_emitted = false;
            }
            else
            {
                // Release
                if (g_pressed)
                {
                    const uint32_t press_ms_u32 = (uint32_t)(now_ms - g_down_ms);
                    const uint16_t press_ms     = clamp_u16(press_ms_u32);
                    g_last_press_ms             = press_ms;

                    // If we already emitted LONG during hold, do not emit again.
                    if (!g_long_emitted)
                    {
                        if (press_ms >= (uint16_t)T_BTN_SHORT_MIN_MS &&
                            press_ms <  (uint16_t)T_BTN_GRACE_MS)
                        {
                            emit(now_ms, EV_BTN_SHORT_PRESS, SRC_BUTTON, EVR_INTERNAL, press_ms, 0);
                        }
                        else if (press_ms >= (uint16_t)T_BTN_GRACE_MS)
                        {
                            // Long press released before we hit the early-emit path (e.g. low poll rate).
                            emit(now_ms, EV_BTN_LONG_PRESS, SRC_BUTTON, EVR_INTERNAL, press_ms, 0);
                            g_long_emitted = true;
                        }
                        else
                        {
                            // Too short: ignore
                        }
                    }
                }

                // Reset for next press
                g_pressed      = false;
                g_down_ms      = 0;
                g_long_emitted = false;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Grace-hold early emit (software shutdown before LTC nuclear)
    // -------------------------------------------------------------------------
    if (g_pressed && !g_long_emitted)
    {
        const uint32_t held_ms = (uint32_t)(now_ms - g_down_ms);

        if (held_ms >= (uint32_t)T_BTN_GRACE_MS)
        {
            emit(now_ms, EV_BTN_LONG_PRESS, SRC_BUTTON, EVR_TIMEOUT, clamp_u16(held_ms), 0);
            g_long_emitted = true;
        }

        // T_BTN_NUCLEAR_MS is hardware-enforced by LTC; do not attempt to “outsmart” it here.
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
