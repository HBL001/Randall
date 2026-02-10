// fuel_gauge.cpp

#include "fuel_gauge.h"

#include <Arduino.h>

#include "pins.h"
#include "thresholds.h"
#include "event_queue.h"

// -----------------------------------------------------------------------------
// Internal configuration (module-local, not global macros)
// -----------------------------------------------------------------------------
static const uint16_t kSamplePeriodMs   = 200;  // how often we take an ADC reading
static const uint8_t  kStableSamplesReq = 3;    // consecutive samples required to accept a change

// -----------------------------------------------------------------------------
// Internal state
// -----------------------------------------------------------------------------
static uint32_t        g_next_sample_ms = 0;

static uint16_t        g_last_adc       = 0;

static battery_state_t g_reported_state = BAT_UNKNOWN;
static battery_state_t g_candidate_state = BAT_UNKNOWN;
static uint8_t         g_candidate_count = 0;

static bool            g_lockout_active = false;
static bool            g_lockout_candidate = false;
static uint8_t         g_lockout_candidate_count = 0;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static inline void emit_bat_event(uint32_t now_ms,
                                  event_id_t id,
                                  event_reason_t reason,
                                  uint16_t arg0,
                                  uint16_t arg1)
{
    event_t e;
    e.t_ms   = now_ms;
    e.id     = id;
    e.src    = SRC_BATTERY;
    e.reason = reason;
    e.arg0   = arg0;
    e.arg1   = arg1;
    (void)eventq_push(&e);
}

static inline battery_state_t classify_battery(uint16_t adc)
{
    // Ordered high -> low; uses thresholds.h exactly (no aliases).
    if (adc >= ADC_FULL)     return BAT_FULL;
    if (adc >= ADC_HALF)     return BAT_HALF;
    if (adc >= ADC_LOW)      return BAT_LOW;
    // Below LOW: CRITICAL bucket (includes adc < ADC_CRITICAL too)
    return BAT_CRITICAL;
}

static inline bool classify_lockout(bool currently_lockout, uint16_t adc)
{
    // Hysteretic lockout decision uses thresholds.h exactly.
    if (!currently_lockout)
    {
        // Enter lockout at or below enter threshold.
        if (adc <= ADC_LOCKOUT_ENTER) return true;
        return false;
    }
    else
    {
        // Exit lockout only when we recover to or above exit threshold.
        if (adc >= ADC_LOCKOUT_EXIT) return false;
        return true;
    }
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------
void fuel_gauge_init(void)
{
    // Ensure ADC pin is input (Arduino does this by default, but keep explicit).
    pinMode(PIN_FUELGAUGE_ADC, INPUT);

    g_next_sample_ms = 0;

    g_last_adc = 0;

    g_reported_state = BAT_UNKNOWN;
    g_candidate_state = BAT_UNKNOWN;
    g_candidate_count = 0;

    g_lockout_active = false;
    g_lockout_candidate = false;
    g_lockout_candidate_count = 0;
}

void fuel_gauge_poll(uint32_t now_ms)
{
    if ((int32_t)(now_ms - g_next_sample_ms) < 0)
        return;

    g_next_sample_ms = now_ms + kSamplePeriodMs;

    // Take one ADC sample (0..1023).
    const uint16_t adc = (uint16_t)analogRead(PIN_FUELGAUGE_ADC);
    g_last_adc = adc;

    // -------------------------
    // Battery state classification with stability requirement
    // -------------------------
    const battery_state_t s = classify_battery(adc);

    if (s != g_candidate_state)
    {
        g_candidate_state = s;
        g_candidate_count = 1;
    }
    else
    {
        if (g_candidate_count < 255) g_candidate_count++;
    }

    if (g_candidate_state != g_reported_state && g_candidate_count >= kStableSamplesReq)
    {
        g_reported_state = g_candidate_state;
        emit_bat_event(now_ms, EV_BAT_STATE_CHANGED, EVR_CLASSIFIER_STABLE,
                       (uint16_t)g_reported_state, adc);
    }

    // -------------------------
    // Lockout hysteresis + stability requirement
    // -------------------------
    const bool lockout_now = classify_lockout(g_lockout_active, adc);

    if (lockout_now != g_lockout_candidate)
    {
        g_lockout_candidate = lockout_now;
        g_lockout_candidate_count = 1;
    }
    else
    {
        if (g_lockout_candidate_count < 255) g_lockout_candidate_count++;
    }

    if (g_lockout_candidate_count >= kStableSamplesReq && g_lockout_candidate != g_lockout_active)
    {
        g_lockout_active = g_lockout_candidate;

        if (g_lockout_active)
        {
            emit_bat_event(now_ms, EV_BAT_LOCKOUT_ENTER, EVR_HYSTERESIS,
                           (uint16_t)g_reported_state, adc);
        }
        else
        {
            emit_bat_event(now_ms, EV_BAT_LOCKOUT_EXIT, EVR_HYSTERESIS,
                           (uint16_t)g_reported_state, adc);
        }
    }
}

uint16_t fuel_gauge_last_adc(void)
{
    return g_last_adc;
}

battery_state_t fuel_gauge_last_state(void)
{
    return g_reported_state;
}

bool fuel_gauge_lockout_active(void)
{
    return g_lockout_active;
}
