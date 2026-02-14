// drv_dvr_status.cpp
//
// Minimal DVR status discriminator (LED pattern -> semantic DVR events)
//
// Inputs (consumed from event_queue, preserved for others):
//   - EV_DVR_LED_PATTERN_CHANGED (arg0 = dvr_led_pattern_t)
//
// Outputs (emitted into event_queue):
//   - EV_DVR_RECORD_STARTED
//   - EV_DVR_RECORD_STOPPED
//   - EV_DVR_POWERED_OFF
//   - EV_DVR_POWERED_ON_IDLE
//   - EV_DVR_ERROR  (arg0 = error_code_t, arg1 = last dvr_led_pattern_t)
//
// Notes:
//   - Purely LED-driven, no new timing constants. Uses T_BOOT_TIMEOUT_MS.
//   - High-value discriminator:
//       FAST_BLINK persisting beyond window => ERR_DVR_CARD_ERROR
//   - Preserves all non-LED events by stashing + re-pushing.
//   - Deterministic: emits only on pattern changes and one-shot error.
//
// IMPORTANT UPDATE (semantic correctness):
//   - RECORD_STOPPED is emitted only when we positively see recording end
//     (SLOW_BLINK -> SOLID or OFF). FAST_BLINK/UNKNOWN no longer generate STOPPED.

#include "drv_dvr_status.h"

#include <Arduino.h>

#include "enums.h"
#include "event_queue.h"
#include "timings.h"

// -----------------------------------------------------------------------------
// Internal state
// -----------------------------------------------------------------------------
static dvr_led_pattern_t s_last_pat = DVR_LED_UNKNOWN;

// Persistent fast-blink discriminator
static bool     s_fast_persist_armed = false;
static uint32_t s_fast_deadline_ms   = 0;
static bool     s_sd_error_emitted   = false;   // one-shot latch while FAST persists

// Recording latch (derived; not authoritative)
static bool s_recording = false;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static inline bool time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static inline void emit_event(uint32_t now_ms,
                              event_id_t id,
                              event_reason_t reason,
                              uint16_t arg0,
                              uint16_t arg1)
{
    event_t e;
    e.t_ms   = now_ms;
    e.id     = id;

    // This is a semantic interpreter; keep enums stable by using an existing src.
    // If you later add SRC_DVR_STATUS, switch to it.
    e.src    = SRC_FSM;
    e.reason = reason;

    e.arg0   = arg0;
    e.arg1   = arg1;
    (void)eventq_push(&e);
}

// Conservative “normalising” patterns that should cancel SD suspicion.
static inline bool cancels_fast_persist(dvr_led_pattern_t p)
{
    return (p == DVR_LED_OFF) || (p == DVR_LED_SOLID) || (p == DVR_LED_UNKNOWN);
}

static void arm_fast_persist(uint32_t now_ms)
{
    s_fast_persist_armed = true;
    s_fast_deadline_ms   = now_ms + (uint32_t)T_BOOT_TIMEOUT_MS;
    s_sd_error_emitted   = false;   // new fast-blink episode => allow one-shot again
}

static void disarm_fast_persist(void)
{
    s_fast_persist_armed = false;
    s_fast_deadline_ms   = 0;
    s_sd_error_emitted   = false;
}

// Emit semantic events derived from a *transition* (prev -> pat)
static void on_pattern_transition(uint32_t now_ms, dvr_led_pattern_t prev, dvr_led_pattern_t pat)
{
    // -------------------------------------------------------------------------
    // 1) Recording latch:
    //    - Start: entering SLOW_BLINK
    //    - Stop: leaving SLOW_BLINK to SOLID or OFF (positive confirmation)
    // -------------------------------------------------------------------------
    if (pat == DVR_LED_SLOW_BLINK)
    {
        if (!s_recording)
        {
            s_recording = true;
            emit_event(now_ms, EV_DVR_RECORD_STARTED, EVR_CLASSIFIER_STABLE, 0, 0);
        }
    }
    else
    {
        // Only declare "stop" when we have a plausible post-record state.
        if (s_recording && prev == DVR_LED_SLOW_BLINK)
        {
            if (pat == DVR_LED_SOLID || pat == DVR_LED_OFF)
            {
                s_recording = false;
                emit_event(now_ms, EV_DVR_RECORD_STOPPED, EVR_CLASSIFIER_STABLE, 0, 0);
            }
            // If we land in FAST_BLINK/UNKNOWN, don't claim stop — it's error/transitional.
        }
    }

    // -------------------------------------------------------------------------
    // 2) Power state hints
    // -------------------------------------------------------------------------
    if (pat == DVR_LED_OFF)
    {
        emit_event(now_ms, EV_DVR_POWERED_OFF, EVR_CLASSIFIER_STABLE, 0, 0);
        // FAST->OFF is a normal shutdown signature; not an SD error.
        disarm_fast_persist();
    }
    else if (pat == DVR_LED_SOLID)
    {
        emit_event(now_ms, EV_DVR_POWERED_ON_IDLE, EVR_CLASSIFIER_STABLE, 0, 0);
        // SOLID implies "normal", so cancel SD suspicion.
        disarm_fast_persist();
    }

    // -------------------------------------------------------------------------
    // 3) SD card discriminator: FAST_BLINK persistence
    //
    // Arm on entering FAST_BLINK, keep armed while it persists.
    // Disarm on OFF/SOLID/UNKNOWN or any other non-fast pattern.
    // -------------------------------------------------------------------------
    if (pat == DVR_LED_FAST_BLINK)
    {
        if (!s_fast_persist_armed)
            arm_fast_persist(now_ms);
    }
    else if (cancels_fast_persist(pat))
    {
        disarm_fast_persist();
    }
    else
    {
        disarm_fast_persist();
    }
}

static void poll_led_pattern_events(uint32_t now_ms)
{
    // Preserve non-LED events: stash + re-push
    enum { STASH_MAX = 16 };
    event_t stash[STASH_MAX];
    uint8_t n = 0;

    event_t ev;
    while (eventq_pop(&ev))
    {
        if (ev.id == EV_DVR_LED_PATTERN_CHANGED)
        {
            const dvr_led_pattern_t pat = (dvr_led_pattern_t)(ev.arg0 & 0xFFu);

            if (pat != s_last_pat)
            {
                const dvr_led_pattern_t prev = s_last_pat;
                s_last_pat = pat;
                on_pattern_transition(now_ms, prev, pat);
            }
            continue;
        }

        if (n < STASH_MAX)
            stash[n++] = ev;
        else
            break; // extremely rare; remaining events are dropped
    }

    for (uint8_t i = 0; i < n; i++)
        (void)eventq_push(&stash[i]);
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------
void drv_dvr_status_init(void)
{
    s_last_pat = DVR_LED_UNKNOWN;
    s_recording = false;
    disarm_fast_persist();
}

void drv_dvr_status_poll(uint32_t now_ms)
{
    poll_led_pattern_events(now_ms);

    // SD card error discriminator: FAST_BLINK that does NOT resolve within window
    if (s_fast_persist_armed && !s_sd_error_emitted && time_reached(now_ms, s_fast_deadline_ms))
    {
        // Emit a semantic DVR error:
        //   arg0 = error_code_t (ERR_DVR_CARD_ERROR)
        //   arg1 = last LED pattern (audit)
        emit_event(now_ms,
                   EV_DVR_ERROR,
                   EVR_TIMEOUT,
                   (uint16_t)ERR_DVR_CARD_ERROR,
                   (uint16_t)s_last_pat);

        // One-shot: latch while this FAST episode continues.
        s_sd_error_emitted = true;
    }
}

dvr_led_pattern_t drv_dvr_status_last_led_pattern(void)
{
    return s_last_pat;
}

bool drv_dvr_status_recording_assumed(void)
{
    return s_recording;
}
