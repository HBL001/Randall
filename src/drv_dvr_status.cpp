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
//     (RunCam "missing microSD" tends to be persistent fast blink.)
//   - Preserves all non-LED events by stashing + re-pushing.
//   - Deterministic: emits only on pattern changes and one-shot error.
//
// Integration order in loop():
//   drv_dvr_led_poll(now_ms);        // produces EV_DVR_LED_PATTERN_CHANGED
//   drv_dvr_status_poll(now_ms);     // consumes it, emits semantic DVR events

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
    e.src    = SRC_FSM;                // reuse existing source; keeps enums stable
    e.reason = reason;
    e.arg0   = arg0;
    e.arg1   = arg1;
    (void)eventq_push(&e);
}

// Conservative “normalising” patterns that should cancel SD suspicion.
// (OFF is shutdown complete; SOLID is idle; UNKNOWN is transitional.)
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

static void on_pattern_changed(uint32_t now_ms, dvr_led_pattern_t pat)
{
    // 1) Recording latch (SLOW_BLINK = recording)
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
        if (s_recording)
        {
            s_recording = false;
            emit_event(now_ms, EV_DVR_RECORD_STOPPED, EVR_CLASSIFIER_STABLE, 0, 0);
        }
    }

    // 2) Power state hints
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

    // 3) SD card discriminator: FAST_BLINK persistence
    //
    // Important nuance:
    // - During shutdown, you may see FAST_BLINK then OFF fairly quickly.
    // - In “missing microSD” error, FAST_BLINK tends to persist indefinitely.
    //
    // Therefore:
    // - Arm the timer when entering FAST_BLINK.
    // - Keep it armed while FAST_BLINK continues.
    // - Disarm on OFF/SOLID/UNKNOWN (normalising patterns).
    if (pat == DVR_LED_FAST_BLINK)
    {
        // If this is a *new* episode, arm it; if already armed, leave deadline alone.
        if (!s_fast_persist_armed)
            arm_fast_persist(now_ms);
    }
    else if (cancels_fast_persist(pat))
    {
        disarm_fast_persist();
    }
    else
    {
        // For ABNORMAL_BOOT or other non-fast patterns: cancel suspicion.
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
                s_last_pat = pat;
                on_pattern_changed(now_ms, pat);
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
