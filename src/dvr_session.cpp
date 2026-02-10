// dvr_session.cpp

#include "dvr_session.h"

#include "timings.h"
#include "action_queue.h"
#include "event_queue.h"   // optional for debug events later (not used now)
#include <Arduino.h>

// -----------------------------------------------------------------------------
// Internal command / confirmation state
// -----------------------------------------------------------------------------
enum dvr_task_t : uint8_t
{
    TASK_NONE = 0,
    TASK_BOOT_WAIT_LED,
    TASK_AUTOREC_WAIT_LED,
    TASK_STARTREC_WAIT_LED,
    TASK_STOPREC_WAIT_LED
};

typedef struct
{
    dvr_led_pattern_t last_led;

    dvr_task_t task;
    uint32_t   task_deadline_ms;

    // Simple actuator pacing guard (since executor provides waveforms but no "done" event)
    bool     cmd_guard_active;
    uint32_t cmd_guard_free_ms;

    // Last error observed at session layer
    error_code_t last_err;

    // Latched intent for auto-record
    bool autorec_pending;

} dvr_session_t;

static dvr_session_t g;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static inline uint32_t now_millis(void) { return millis(); }

static inline bool cmd_allowed(uint32_t now_ms)
{
    return (!g.cmd_guard_active) || (now_ms >= g.cmd_guard_free_ms);
}

static inline void cmd_guard(uint32_t now_ms, uint32_t holdoff_ms)
{
    g.cmd_guard_active = true;
    g.cmd_guard_free_ms = now_ms + holdoff_ms;
}

static inline void action_emit(action_id_t id)
{
    action_t a;
    a.t_enq_ms = now_millis();
    a.id       = id;
    a.arg0     = 0;
    a.arg1     = 0;
    (void)actionq_push(&a);
}

static inline bool led_is_idle(dvr_led_pattern_t p)      { return p == DVR_LED_SOLID; }
static inline bool led_is_recording(dvr_led_pattern_t p) { return p == DVR_LED_SLOW_BLINK; }
static inline bool led_is_off(dvr_led_pattern_t p)       { return p == DVR_LED_OFF; }

static void start_task(dvr_task_t t, uint32_t now_ms, uint32_t timeout_ms)
{
    g.task = t;
    g.task_deadline_ms = now_ms + timeout_ms;
}

static void end_task_ok(void)
{
    g.task = TASK_NONE;
    g.task_deadline_ms = 0;
    g.last_err = ERR_NONE;
}

static void end_task_err(error_code_t err)
{
    g.task = TASK_NONE;
    g.task_deadline_ms = 0;
    g.last_err = err;
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------
void dvr_session_init(void)
{
    g.last_led = DVR_LED_UNKNOWN;

    g.task = TASK_NONE;
    g.task_deadline_ms = 0;

    g.cmd_guard_active = false;
    g.cmd_guard_free_ms = 0;

    g.last_err = ERR_NONE;
    g.autorec_pending = false;
}

void dvr_session_on_led(uint32_t now_ms, dvr_led_pattern_t p)
{
    (void)now_ms;
    g.last_led = p;

    // Immediate error signatures
    if (p == DVR_LED_FAST_BLINK)
    {
        end_task_err(ERR_DVR_CARD_ERROR);
        return;
    }
    if (p == DVR_LED_ABNORMAL_BOOT)
    {
        end_task_err(ERR_DVR_ABNORMAL_BOOT);
        return;
    }

    // Task completion checks
    switch (g.task)
    {
        case TASK_BOOT_WAIT_LED:
            if (led_is_idle(p) || led_is_recording(p))
            {
                end_task_ok();
            }
            break;

        case TASK_AUTOREC_WAIT_LED:
        case TASK_STARTREC_WAIT_LED:
            if (led_is_recording(p))
            {
                end_task_ok();
            }
            break;

        case TASK_STOPREC_WAIT_LED:
            if (led_is_idle(p))
            {
                end_task_ok();
            }
            break;

        default:
            break;
    }
}

void dvr_session_poll(uint32_t now_ms)
{
    // Release command guard when elapsed
    if (g.cmd_guard_active && now_ms >= g.cmd_guard_free_ms)
    {
        g.cmd_guard_active = false;
    }

    // Timeouts
    if (g.task != TASK_NONE && now_ms >= g.task_deadline_ms)
    {
        if (g.task == TASK_BOOT_WAIT_LED)
        {
            end_task_err(ERR_DVR_BOOT_TIMEOUT);
        }
        else
        {
            // start/stop confirmation timeout becomes "unexpected LED pattern"
            // because we requested a change and didn't observe it in time.
            end_task_err(ERR_UNEXPECTED_LED_PATTERN);
        }
    }

    // Auto-record: once boot has completed and we are idle, request start-record.
    if (g.autorec_pending && g.task == TASK_NONE)
    {
        if (led_is_idle(g.last_led))
        {
            // best-effort: enqueue a short press if allowed
            if (cmd_allowed(now_ms))
            {
                action_emit(ACT_DVR_PRESS_SHORT);
                cmd_guard(now_ms, (uint32_t)T_DVR_PRESS_SHORT_MS + (uint32_t)T_DVR_PRESS_GAP_MS);
                start_task(TASK_AUTOREC_WAIT_LED, now_ms, (uint32_t)T_BOOT_TIMEOUT_MS);
                g.autorec_pending = false;
            }
        }
        else if (led_is_recording(g.last_led))
        {
            // Already recording; clear intent.
            g.autorec_pending = false;
        }
    }
}

result_t dvr_session_request_power_on(uint32_t now_ms, bool request_auto_record)
{
    // If already in a stable on-state, accept as no-op.
    if (led_is_idle(g.last_led) || led_is_recording(g.last_led))
    {
        if (request_auto_record && led_is_idle(g.last_led))
        {
            g.autorec_pending = true;
        }
        return RET_OK;
    }

    if (g.task != TASK_NONE)
        return RET_WAIT;

    if (!cmd_allowed(now_ms))
        return RET_WAIT;

    action_emit(ACT_DVR_PRESS_LONG);
    cmd_guard(now_ms, (uint32_t)T_DVR_BOOT_PRESS_MS + (uint32_t)T_DVR_AFTER_PWRON_MS);

    start_task(TASK_BOOT_WAIT_LED, now_ms, (uint32_t)T_BOOT_TIMEOUT_MS);
    g.autorec_pending = request_auto_record;
    g.last_err = ERR_NONE;

    return RET_OK;
}

result_t dvr_session_request_start_record(uint32_t now_ms)
{
    if (led_is_recording(g.last_led))
        return RET_OK;

    if (g.task != TASK_NONE)
        return RET_WAIT;

    if (!cmd_allowed(now_ms))
        return RET_WAIT;

    action_emit(ACT_DVR_PRESS_SHORT);
    cmd_guard(now_ms, (uint32_t)T_DVR_PRESS_SHORT_MS + (uint32_t)T_DVR_PRESS_GAP_MS);

    start_task(TASK_STARTREC_WAIT_LED, now_ms, (uint32_t)T_BOOT_TIMEOUT_MS);
    g.last_err = ERR_NONE;
    return RET_OK;
}

result_t dvr_session_request_stop_record(uint32_t now_ms)
{
    if (led_is_idle(g.last_led))
        return RET_OK;

    if (g.task != TASK_NONE)
        return RET_WAIT;

    if (!cmd_allowed(now_ms))
        return RET_WAIT;

    action_emit(ACT_DVR_PRESS_SHORT);
    cmd_guard(now_ms, (uint32_t)T_DVR_PRESS_SHORT_MS + (uint32_t)T_DVR_PRESS_GAP_MS);

    start_task(TASK_STOPREC_WAIT_LED, now_ms, (uint32_t)T_BOOT_TIMEOUT_MS);
    g.last_err = ERR_NONE;
    return RET_OK;
}

result_t dvr_session_request_power_off(uint32_t now_ms)
{
    // If we're already clearly off, accept.
    if (led_is_off(g.last_led))
        return RET_OK;

    if (g.task != TASK_NONE)
        return RET_WAIT;

    if (!cmd_allowed(now_ms))
        return RET_WAIT;

    action_emit(ACT_DVR_PRESS_LONG);
    cmd_guard(now_ms, (uint32_t)T_DVR_PRESS_LONG_MS + (uint32_t)T_DVR_AFTER_PWROFF_MS);

    // No LED confirmation assumed for off (DVR may go dark after power rail cut)
    g.autorec_pending = false;
    g.last_err = ERR_NONE;
    return RET_OK;
}

dvr_led_pattern_t dvr_session_last_led(void) { return g.last_led; }

bool dvr_session_is_recording(void) { return led_is_recording(g.last_led); }
bool dvr_session_is_idle(void)      { return led_is_idle(g.last_led); }
bool dvr_session_is_off(void)       { return led_is_off(g.last_led); }

bool dvr_session_is_busy(void)
{
    return (g.task != TASK_NONE) || g.cmd_guard_active;
}

error_code_t dvr_session_last_error(void) { return g.last_err; }
