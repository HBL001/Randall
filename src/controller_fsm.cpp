// controller_fsm.cpp
//
// Minimal controller FSM (policy layer)
// - Consumes events from event_queue
// - Emits actions into action_queue
// - Drives presentation via ui_policy on state transitions
//
// Updated policy to match new canonical user story:
// - MEGA is powered by LTC and boots first.
// - On MEGA firmware start, we automatically boot the DVR (self-test) and keep it ON/IDLE after PASS.
// - Single tap toggles record start/stop (DVR remains powered).
// - Long press requests graceful DVR shutdown (stop first if recording), then waits for EV_DVR_POWERED_OFF.
//   (Power cut / KILL# is handled by the power manager layer, not here.)
//
// This version integrates:
//   - Battery critical + lockout handling
//   - Button short/long handling
//   - DVR semantic events (from drv_dvr_status): RECORD_STARTED/STOPPED, POWERED_ON/OFF, DVR_ERROR
//
// Key policy:
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

// Shutdown sequencing
static bool            s_shutdown_pending = false;   // long-hold requested
static bool            s_stop_pending     = false;   // stop requested as part of shutdown

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
static inline void clear_error_if(controller_state_t next)
{
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

            // After lockout clears, conservative fallback to OFF.
            // (System power behaviour is ultimately governed by LTC/power_mgr.)
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

    // If shutdown is pending, ignore all further user input (deterministic, no buffering)
    if (s_shutdown_pending)
        return;

    switch (s_state)
    {
        case STATE_OFF:
        {
            // IMPORTANT CHANGE:
            // MEGA power-up is handled by LTC hardware, not via a firmware “short press”.
            // So in STATE_OFF, ignore button taps here.
            //
            // If your platform generates EV_BTN_* even while “off”, those should be
            // ignored; actual power-on happens before firmware runs.
            (void)now_ms;
            return;
        }

        case STATE_BOOTING:
        {
            // Policy: discard taps while booting/self-test (no buffering)
            return;
        }

        case STATE_IDLE:
        {
            if (is_short)
            {
                // DVR is already ON/IDLE after self-test PASS.
                // Tap toggles into RECORDING (confirmed by EV_DVR_RECORD_STARTED).
                act_dvr_short(now_ms);
            }
            else
            {
                // Long hold => graceful shutdown (DVR long press), then wait for EV_DVR_POWERED_OFF.
                s_shutdown_pending = true;
                act_dvr_long(now_ms);
                // Remain in current state until EV_DVR_POWERED_OFF arrives.
            }
            return;
        }

        case STATE_RECORDING:
        {
            if (is_short)
            {
                // Tap toggles out of recording (confirmed by EV_DVR_RECORD_STOPPED).
                act_dvr_short(now_ms);
            }
            else
            {
                // Long hold => stop recording first, then shutdown DVR.
                s_shutdown_pending = true;
                s_stop_pending     = true;
                act_dvr_short(now_ms);   // request stop; EV_DVR_RECORD_STOPPED will trigger long shutdown
            }
            return;
        }

        case STATE_LOW_BAT:
        {
            // In LOW_BAT, allow long for shutdown; ignore short.
            if (is_long)
            {
                s_shutdown_pending = true;

                // If we are still recording for any reason, request stop first.
                if (s_state == STATE_RECORDING)
                {
                    s_stop_pending = true;
                    act_dvr_short(now_ms);
                }
                else
                {
                    act_dvr_long(now_ms);
                }
            }
            return;
        }

        case STATE_ERROR:
        {
            // In ERROR, allow user to attempt graceful shutdown via long.
            if (is_long)
            {
                s_shutdown_pending = true;
                act_dvr_long(now_ms);
                // Remain in ERROR until EV_DVR_POWERED_OFF arrives; then go OFF.
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
            // Boot/self-test complete confirmation: DVR is ON and idle (solid).
            if (s_state == STATE_BOOTING && !s_lockout)
            {
                s_err = ERR_NONE;
                set_state(now_ms, STATE_IDLE);

                // Ready cue (boot/ready confirmed).
                ui_policy_on_record_confirmed(now_ms); // existing hook; rename later if desired
            }
            return;
        }

        case EV_DVR_POWERED_OFF:
        {
            // DVR has actually powered off (e.g., graceful shutdown complete)
            if (!s_lockout)
            {
                // If we were shutting down, clear pending flags now.
                s_shutdown_pending = false;
                s_stop_pending     = false;

                clear_error_if(STATE_OFF);
                set_state(now_ms, STATE_OFF);
            }
            return;
        }

        case EV_DVR_RECORD_STARTED:
        {
            // Confirm start recording
            if (!s_lockout && !s_shutdown_pending)
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

                // If a shutdown was requested while recording, continue shutdown now.
                if (s_shutdown_pending && s_stop_pending)
                {
                    s_stop_pending = false;
                    act_dvr_long(now_ms);
                }
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
    // IMPORTANT CHANGE:
    // Firmware starts because LTC has already powered the MEGA.
    // Therefore we begin in BOOTING and immediately boot the DVR for self-test.
    s_state = STATE_BOOTING;

    s_bat   = BAT_UNKNOWN;
    s_lockout = false;
    s_err   = ERR_NONE;

    s_shutdown_pending = false;
    s_stop_pending     = false;

    const uint32_t now_ms = 0;
    s_boot_deadline_ms = now_ms + (uint32_t)T_BOOT_TIMEOUT_MS;

    ui_policy_init();
    ui_policy_on_state_enter(0, s_state, s_err, s_bat);

    // Kick off DVR boot gesture immediately (self-test path).
    // If a later battery event puts us into LOCKOUT/LOW_BAT, higher layers can act.
    act_dvr_long(0);
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
