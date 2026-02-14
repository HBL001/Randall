// drv_fuel_gauge.cpp
//
// Driver-level fuel gauge:
// - Samples ADC (PIN_FUELGAUGE_ADC)
// - Classifies into battery_state_t buckets using thresholds.h
// - Applies stability requirement (N consecutive samples) before reporting changes
// - Applies lockout hysteresis (enter/exit thresholds) with the same stability requirement
// - Emits events into event_queue (no policy decisions here)
//
// Event contract (consistent across battery events):
//   arg0 = (uint16_t)battery_state_t  (state at time of event)
//   arg1 = adc reading (0..1023)
//
// Uses existing identifiers from: pins.h, thresholds.h, enums.h, config.h, event_queue.h

#include "drv_fuel_gauge.h"

#include <Arduino.h>

#include "config.h"
#include "pins.h"
#include "thresholds.h"
#include "enums.h"
#include "event_queue.h"

// -----------------------------------------------------------------------------
// Sampling/stability configuration
// -----------------------------------------------------------------------------

// Prefer config.h cadence (already present in your repo).
#ifndef CFG_BATTERY_SAMPLE_MS
static const uint16_t kSamplePeriodMs = 250;
#else
static const uint16_t kSamplePeriodMs = (uint16_t)CFG_BATTERY_SAMPLE_MS;
#endif

// Stability samples (module-local hygiene; do not create new global constants here)
static const uint8_t kStableSamplesReq = 3;

// -----------------------------------------------------------------------------
// Internal state
// -----------------------------------------------------------------------------
static uint32_t        g_next_sample_ms = 0;
static uint16_t        g_last_adc       = 0;

static battery_state_t g_reported_state       = BAT_UNKNOWN;
static battery_state_t g_candidate_state      = BAT_UNKNOWN;
static uint8_t         g_candidate_count      = 0;

static bool            g_lockout_active           = false;
static bool            g_lockout_candidate        = false;
static uint8_t         g_lockout_candidate_count  = 0;

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
    // Ordered high -> low; uses thresholds.h exactly.
    if (adc >= ADC_FULL) return BAT_FULL;
    if (adc >= ADC_HALF) return BAT_HALF;
    if (adc >= ADC_LOW)  return BAT_LOW;
    return BAT_CRITICAL;
}

static inline bool lockout_should_be_active(bool currently_lockout, uint16_t adc)
{
    // Hysteretic lockout decision uses thresholds.h exactly.
    //
    // Enter: adc <= ADC_LOCKOUT_ENTER
    // Exit : adc >= ADC_LOCKOUT_EXIT
    //
    // NOTE: Your previous code had the exit condition inverted.
    if (!currently_lockout)
    {
        return (adc <= ADC_LOCKOUT_ENTER);
    }
    else
    {
        return (adc < ADC_LOCKOUT_EXIT) ? true : false;
        // Equivalent: stay locked out until we reach ADC_LOCKOUT_EXIT or higher.
    }
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------
void drv_fuel_gauge_init(void)
{
    pinMode(PIN_FUELGAUGE_ADC, INPUT);

    g_next_sample_ms = 0;
    g_last_adc       = 0;

    g_reported_state  = BAT_UNKNOWN;
    g_candidate_state = BAT_UNKNOWN;
    g_candidate_count = 0;

    g_lockout_active          = false;
    g_lockout_candidate       = false;
    g_lockout_candidate_count = 0;
}

void drv_fuel_gauge_poll(uint32_t now_ms)
{
    if ((int32_t)(now_ms - g_next_sample_ms) < 0)
        return;

    g_next_sample_ms = now_ms + (uint32_t)kSamplePeriodMs;

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

        // arg0=state, arg1=adc
        emit_bat_event(now_ms,
                       EV_BAT_STATE_CHANGED,
                       EVR_CLASSIFIER_STABLE,
                       (uint16_t)g_reported_state,
                       adc);
    }

    // -------------------------
    // Lockout hysteresis + stability requirement
    // -------------------------
    const bool lockout_now = lockout_should_be_active(g_lockout_active, adc);

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

        // arg0=state (at time), arg1=adc
        emit_bat_event(now_ms,
                       g_lockout_active ? EV_BAT_LOCKOUT_ENTER : EV_BAT_LOCKOUT_EXIT,
                       EVR_HYSTERESIS,
                       (uint16_t)g_reported_state,
                       adc);
    }
}

uint16_t drv_fuel_gauge_last_adc(void)
{
    return g_last_adc;
}

battery_state_t drv_fuel_gauge_last_state(void)
{
    return g_reported_state;
}

bool drv_fuel_gauge_lockout_active(void)
{
    return g_lockout_active;
}
