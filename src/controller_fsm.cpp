// controller_fsm.cpp
//
// Minimal controller FSM (policy layer)
// - Consumes events from event_queue
// - Emits actions into action_queue
// - Drives presentation via ui_policy on state transitions
//
// This version integrates:
//   - Battery critical + lockout handling
//   - Button short/long handling
//   - DVR semantic events (from drv_dvr_status): RECORD_STARTED/STOPPED, POWERED_ON/OFF, DVR_ERROR
//
// Key policy upgrades vs “optimistic” version:
//   - Boot completion is LED-confirmed (EV_DVR_POWERED_ON_IDLE), not timer-assumed.
//   - Recording start/stop confirmations are LED-confirmed (EV_DVR_RECORD_*).
//   - SD-card missing / persistent FAST blink becomes EV_DVR_ERROR(ERR_DVR_CARD_ERROR) -> STATE_ERROR.
//
// Notes:
// - No new timing constants: uses T_BOOT_TIMEOUT_MS only.
// - Deterministic: ignores button gestures while booting; ignores illegal record toggles.
// - Does not invent sources/reasons: uses enums.h values; does not peek into action queue.

#include "controller_fsm.h"

#include <Arduino.h>

#include "enums.h"
#include "event_queue.h"
#include "action_queue.h"
#include "timings.h"
#include "ui_policy.h"

// -----------------------------------------------------------------------------
// Internal state
// -----------------------------------------------------------------------------
static controller_state_t s_state = STATE_OFF;

static battery_state_t s_bat     = BAT_UNKNOWN;
static bool            s_lockout = false;

static error_code_t    s_err     = ERR_NONE;

// Boot confirmation window (await EV_DVR_POWERED_ON_IDLE)
static uint32_t        s_boot_deadline_ms = 0;

// -----------------------------------------------------------------------------
// Helpers: time compare
// -----------------------------------------------------------------------------
static inline bool time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

// -----------------------------------------------------------------------------
// Helpers: action emit
// -----------------------------------------------------------------------------
static inline void emit_action(uint32_t now_ms, action_id_t id, uint16_t arg0, uint16_t arg1)
{
    action_t a;
    a.t_enq_ms = now_ms;
    a.id       = id;
    a.arg0     = arg0;
    a.arg1     = arg1;
    (void)actionq_push(&a);
}

static inline void act_dvr_short(uint32_t now_ms) { emit_action(now_ms, ACT_DVR_PRESS_SHORT, 0, 0); }
static inline void act_dvr_long (uint32_t now_ms) { emit_action(now_ms, ACT_DVR_PRESS_LONG,  0, 0); }

// -----------------------------------------------------------------------------
// State + error transitions (UI policy on entry)
// -----------------------------------------------------------------------------
static inline void set_state(uint32_t now_ms, controller_state_t next)
{
    if (next == s_state)
        return;

    s_state = next;
    ui_policy_on_state_enter(now_ms, s_state, s_err, s_bat);
}

static inline void set_error(uint32_t now_ms, error_code_t err, controller_state_t next_state)
{
    s_err = err;
    ui_policy_on_error(now_ms, s_err);
    set_state(now_ms, next_state);
}

// Convenience: clear error if we are leaving ERROR-like situations
static inline void clear_error_if(uint32_t now_ms, controller_state_t next)
{
    (void)now_ms;
    if (s_err != ERR_NONE && next != STATE_ERROR && next != STATE_LOCKOUT)
        s_err = ERR_NONE;
}

// -----------------------------------------------------------------------------
// Event handling: battery (dominant)
// -----------------------------------------------------------------------------
static void handle_battery_event(uint32_t now_ms, const event_t* ev)
{
    switch (ev->id)
    {
        case EV_BAT_STATE_CHANGED:
        {
            s_bat = (battery_state_t)(ev->arg0 & 0xFFu);

            // CRITICAL battery -> presentation state unless lockout dominates
            if (!s_lockout && s_bat == BAT_CRITICAL)
            {
                s_err = ERR_BAT_CRITICAL;
                set_state(now_ms, STATE_LOW_BAT);
            }
            return;
        }

        case EV_BAT_LOCKOUT_ENTER:
        {
            s_lockout = true;
            s_err     = ERR_BAT_LOCKOUT;
            set_state(now_ms, STATE_LOCKOUT);
            return;
        }

        case EV_BAT_LOCKOUT_EXIT:
        {
            s_lockout = false;
            s_err     = ERR_NONE;

            // Conservative: after lockout clears, go OFF; user can power on again.
            set_state(now_ms, STATE_OFF);
            return;
        }

        default:
            return;
    }
}

// -----------------------------------------------------------------------------
// Event handling: button gestures (user intent)
// -----------------------------------------------------------------------------
static void handle_button_event(uint32_t now_ms, const event_t* ev)
{
    const bool is_short = (ev->id == EV_BTN_SHORT_PRESS);
    const bool is_long  = (ev->id == EV_BTN_LONG_PRESS);

    if (!is_short && !is_long)
        return;

    // LOCKOUT dominates: ignore all button commands
    if (s_lockout)
        return;

    switch (s_state)
    {
        case STATE_OFF:
        {
            if (!is_short)
                return;

            // Power on request -> long press to DVR
            act_dvr_long(now_ms);

            s_err = ERR_NONE;
            set_state(now_ms, STATE_BOOTING);
            s_boot_deadline_ms = now_ms + (uint32_t)T_BOOT_TIMEOUT_MS;
            return;
        }

        case STATE_BOOTING:
        {
            // Policy: discard taps while booting (no buffering)
            return;
        }

        case STATE_IDLE:
        {
            if (is_short)
            {
                // Request start recording; confirmation arrives from EV_DVR_RECORD_STARTED.
                act_dvr_short(now_ms);

                // DO NOT transition to RECORDING yet: wait for LED-confirmation event.
                // Keep UI minimal here; if you want a “click” you can add it in ui_policy later.
            }
            else
            {
                // Grace/long => power off
                act_dvr_long(now_ms);
                clear_error_if(now_ms, STATE_OFF);
                set_state(now_ms, STATE_OFF);
            }
            return;
        }

        case STATE_RECORDING:
        {
            if (is_short)
            {
                // Request stop recording; confirmation arrives from EV_DVR_RECORD_STOPPED.
                act_dvr_short(now_ms);
            }
            else
            {
                // Grace/long => power off
                act_dvr_long(now_ms);
                clear_error_if(now_ms, STATE_OFF);
                set_state(now_ms, STATE_OFF);
            }
            return;
        }

        case STATE_LOW_BAT:
        {
            // Minimal: allow OFF via long; ignore short (prevents starting recording in low bat)
            if (is_long)
            {
                act_dvr_long(now_ms);
                clear_error_if(now_ms, STATE_OFF);
                set_state(now_ms, STATE_OFF);
            }
            return;
        }

        case STATE_ERROR:
        {
            // In ERROR, allow user to power-off via long (escape hatch).
            if (is_long)
            {
                act_dvr_long(now_ms);
                // remain in error until DVR actually powers off (EV_DVR_POWERED_OFF),
                // or just drop to OFF immediately (choose one). We'll drop immediately:
                set_state(now_ms, STATE_OFF);
                s_err = ERR_NONE;
            }
            return;
        }

        case STATE_LOCKOUT:
        default:
            return;
    }
}

// -----------------------------------------------------------------------------
// Event handling: DVR semantic events (from drv_dvr_status)
// -----------------------------------------------------------------------------
static void handle_dvr_semantic_event(uint32_t now_ms, const event_t* ev)
{
    switch (ev->id)
    {
        case EV_DVR_POWERED_ON_IDLE:
        {
            // Boot complete confirmation
            if (s_state == STATE_BOOTING && !s_lockout)
            {
                s_err = ERR_NONE;
                set_state(now_ms, STATE_IDLE);

                // User-story: “ready” cue
                // (This is NOT “record confirmed”; it's boot/ready confirmed.)
                ui_policy_on_record_confirmed(now_ms); // If you dislike this, add a dedicated hook later.
            }
            return;
        }

        case EV_DVR_POWERED_OFF:
        {
            if (!s_lockout)
            {
                s_err = ERR_NONE;
                set_state(now_ms, STATE_OFF);
            }
            return;
        }

        case EV_DVR_RECORD_STARTED:
        {
            // Confirm start recording
            if (!s_lockout)
            {
                set_state(now_ms, STATE_RECORDING);
                ui_policy_on_record_confirmed(now_ms);
            }
            return;
        }

        case EV_DVR_RECORD_STOPPED:
        {
            // Confirm stop recording
            if (!s_lockout)
            {
                set_state(now_ms, STATE_IDLE);
                ui_policy_on_stop_confirmed(now_ms);
            }
            return;
        }

        case EV_DVR_ERROR:
        {
            // arg0 = error_code_t, arg1 = last LED pattern (audit)
            const error_code_t derr = (error_code_t)(ev->arg0 & 0xFFu);

            // SD-card missing is a real user-visible error state (RunCam behaviour)
            set_error(now_ms, derr, STATE_ERROR);
            return;
        }

        default:
            return;
    }
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------
void controller_fsm_init(void)
{
    s_state = STATE_OFF;
    s_bat   = BAT_UNKNOWN;
    s_lockout = false;
    s_err   = ERR_NONE;
    s_boot_deadline_ms = 0;

    ui_policy_init();
    ui_policy_on_state_enter(0, s_state, s_err, s_bat);
}

void controller_fsm_poll(uint32_t now_ms)
{
    // Boot timeout fallback:
    // - If we don't receive EV_DVR_POWERED_ON_IDLE by deadline, treat as boot timeout error.
    if (s_state == STATE_BOOTING && time_reached(now_ms, s_boot_deadline_ms))
    {
        if (!s_lockout)
            set_error(now_ms, ERR_DVR_BOOT_TIMEOUT, STATE_ERROR);
    }

    event_t ev;
    while (eventq_pop(&ev))
    {
        // Battery first (dominant)
        if (ev.id == EV_BAT_STATE_CHANGED ||
            ev.id == EV_BAT_LOCKOUT_ENTER ||
            ev.id == EV_BAT_LOCKOUT_EXIT)
        {
            handle_battery_event(now_ms, &ev);
            continue;
        }

        // DVR semantic events (from drv_dvr_status)
        if (ev.id == EV_DVR_POWERED_ON_IDLE ||
            ev.id == EV_DVR_RECORD_STARTED  ||
            ev.id == EV_DVR_RECORD_STOPPED  ||
            ev.id == EV_DVR_POWERED_OFF     ||
            ev.id == EV_DVR_ERROR)
        {
            handle_dvr_semantic_event(now_ms, &ev);
            continue;
        }

        // Button gestures
        if (ev.id == EV_BTN_SHORT_PRESS || ev.id == EV_BTN_LONG_PRESS)
        {
            handle_button_event(now_ms, &ev);
            continue;
        }

        // Ignore other events for now
    }
}

controller_state_t controller_fsm_state(void)
{
    return s_state;
}

battery_state_t controller_fsm_battery_state(void)
{
    return s_bat;
}

bool controller_fsm_lockout_active(void)
{
    return s_lockout;
}

error_code_t controller_fsm_error(void)
{
    return s_err;
}
