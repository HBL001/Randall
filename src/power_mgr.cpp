// power_mgr.cpp

#include "power_mgr.h"

#include <Arduino.h>

#include "pins.h"
#include "timings.h"
#include "event_queue.h"

// -----------------------------------------------------------------------------
// Internal state (ISR writes minimal data; main context does the rest)
// -----------------------------------------------------------------------------
static volatile uint8_t  s_int_level              = HIGH;   // raw pin level
static volatile bool     s_int_level_changed      = false;
static volatile uint32_t s_int_change_t_ms        = 0;

static uint8_t  g_last_stable_level              = HIGH;
static uint32_t g_last_edge_ms                   = 0;

static bool     g_press_active                   = false;
static uint32_t g_press_down_ms                  = 0;

static bool     g_grace_long_emitted             = false;   // EV_BTN_LONG_PRESS emitted while still held
static uint16_t g_last_press_ms                  = 0;

static bool     g_kill_asserted                  = false;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static inline void emit_event(uint32_t now_ms,
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

static inline uint8_t read_int_level_fast(void)
{
    // digitalRead() is fine for INT0 edges; keep it consistent with existing codebase.
    return (uint8_t)digitalRead(PIN_LTC_INT_N);
}

// -----------------------------------------------------------------------------
// ISR: record INT# level changes and enqueue raw edge events
// -----------------------------------------------------------------------------
static void isr_ltc_int_change()
{
    const uint8_t level = read_int_level_fast();

    if (level == s_int_level)
        return;

    s_int_level         = level;
    s_int_level_changed = true;
    s_int_change_t_ms   = millis();

    // Emit raw INT edge event immediately (ISR-safe queue push).
    event_t e;
    e.t_ms   = s_int_change_t_ms;
    e.src    = SRC_LTC;
    e.arg0   = (uint16_t)level;
    e.arg1   = 0;

    // INT# is typically active-low; pins.h defines LTC_INT_ASSERT_LEVEL.
    e.reason = (level == LTC_INT_ASSERT_LEVEL) ? EVR_EDGE_FALL : EVR_EDGE_RISE;
    e.id     = (level == LTC_INT_ASSERT_LEVEL) ? EV_LTC_INT_ASSERTED : EV_LTC_INT_DEASSERTED;

    (void)eventq_push_isr(&e);
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------
void power_mgr_init(void)
{
    // pins_init() is the authoritative GPIO init for the project.
    // This module assumes pins_init() has been called in setup().
    // We still ensure KILL is in a known state.
    power_mgr_kill_deassert();

    g_last_stable_level   = (uint8_t)digitalRead(PIN_LTC_INT_N);
    s_int_level           = g_last_stable_level;
    s_int_level_changed   = false;
    s_int_change_t_ms     = millis();
    g_last_edge_ms        = s_int_change_t_ms;

    g_press_active        = (g_last_stable_level == LTC_INT_ASSERT_LEVEL);
    g_press_down_ms       = g_press_active ? s_int_change_t_ms : 0;
    g_grace_long_emitted  = false;
    g_last_press_ms       = 0;

    attachInterrupt(digitalPinToInterrupt(PIN_LTC_INT_N), isr_ltc_int_change, CHANGE);
}

void power_mgr_poll(uint32_t now_ms)
{
    // -------------------------------------------------------------------------
    // 1) Debounced edge handling (main-context interpretation)
    // -------------------------------------------------------------------------
    if (s_int_level_changed)
    {
        // Snapshot volatile fields atomically enough for AVR single-byte/bool,
        // but s_int_change_t_ms is 32-bit; copy with interrupts briefly off.
        uint8_t  level;
        uint32_t t_ms;

#ifdef __AVR__
        uint8_t sreg = SREG;
        cli();
        level             = s_int_level;
        t_ms              = s_int_change_t_ms;
        s_int_level_changed = false;
        SREG = sreg;
#else
        level = s_int_level;
        t_ms  = s_int_change_t_ms;
        s_int_level_changed = false;
#endif

        // Debounce: ignore edges too close together.
        if ((uint32_t)(t_ms - g_last_edge_ms) >= (uint32_t)T_BTN_DEBOUNCE_MS)
        {
            g_last_edge_ms      = t_ms;
            g_last_stable_level = level;

            if (level == LTC_INT_ASSERT_LEVEL)
            {
                // Button down (start of press)
                g_press_active       = true;
                g_press_down_ms      = t_ms;
                g_grace_long_emitted = false;
            }
            else
            {
                // Button up (end of press)
                if (g_press_active)
                {
                    const uint32_t press_ms_u32 = (uint32_t)(t_ms - g_press_down_ms);
                    const uint16_t press_ms = (press_ms_u32 > 0xFFFFu) ? 0xFFFFu : (uint16_t)press_ms_u32;
                    g_last_press_ms = press_ms;

                    // If we already emitted a grace-long while held, do not emit again on release.
                    if (!g_grace_long_emitted)
                    {
                        if (press_ms >= (uint16_t)T_BTN_SHORT_MIN_MS &&
                            press_ms <  (uint16_t)T_BTN_GRACE_MS)
                        {
                            emit_event(t_ms, EV_BTN_SHORT_PRESS, SRC_BUTTON, EVR_INTERNAL, press_ms, 0);
                        }
                        else if (press_ms >= (uint16_t)T_BTN_GRACE_MS)
                        {
                            emit_event(t_ms, EV_BTN_LONG_PRESS, SRC_BUTTON, EVR_INTERNAL, press_ms, 0);
                        }
                        else
                        {
                            // Too short; ignore (bounce/noise).
                        }
                    }
                }

                g_press_active       = false;
                g_press_down_ms      = 0;
                g_grace_long_emitted = false;
            }
        }
    }

    // -------------------------------------------------------------------------
    // 2) Grace-hold early emission (software shutdown path)
    //    If user keeps holding, we emit EV_BTN_LONG_PRESS as soon as grace time
    //    is reached, so the FSM can stop-record and power-off *before* LTC "nuclear".
    // -------------------------------------------------------------------------
    if (g_press_active && !g_grace_long_emitted)
    {
        const uint32_t held_ms = (uint32_t)(now_ms - g_press_down_ms);

        if (held_ms >= (uint32_t)T_BTN_GRACE_MS)
        {
            const uint16_t press_ms = (held_ms > 0xFFFFu) ? 0xFFFFu : (uint16_t)held_ms;
            emit_event(now_ms, EV_BTN_LONG_PRESS, SRC_BUTTON, EVR_TIMEOUT, press_ms, 0);
            g_grace_long_emitted = true;
        }

        // NOTE: T_BTN_NUCLEAR_MS is hardware-enforced; after that, the board may die.
        // We intentionally do not attempt to fight it here.
    }
}

void power_mgr_kill_assert(void)
{
    KILL_ASSERT();
    g_kill_asserted = true;
}

void power_mgr_kill_deassert(void)
{
    KILL_DEASSERT();
    g_kill_asserted = false;
}

bool power_mgr_kill_is_asserted(void)
{
    return g_kill_asserted;
}

bool power_mgr_int_is_asserted(void)
{
    const uint8_t level = (uint8_t)digitalRead(PIN_LTC_INT_N);
    return (level == LTC_INT_ASSERT_LEVEL);
}

uint32_t power_mgr_last_press_ms(void)
{
    return (uint32_t)g_last_press_ms;
}
