// controller_fsm.cpp

#include "controller_fsm.h"

// =============================================================================
// Internal state
// =============================================================================

typedef struct
{
    controller_state_t state;

    // Latest observations
    battery_state_t   bat_state;
    dvr_led_pattern_t dvr_led;

    // Latches / policy
    bool lockout_latched;

    // Error bookkeeping
    error_code_t last_error;

    // State entry time (for timeouts / pacing)
    uint32_t state_entry_ms;

    // Pending confirmation window (e.g., "we requested record start; waiting for LED")
    bool     confirm_active;
    uint32_t confirm_deadline_ms;
    dvr_led_pattern_t confirm_expected_a;   // first acceptable outcome
    dvr_led_pattern_t confirm_expected_b;   // second acceptable outcome (optional)

    // DVR command pacing guard (prevents command spam before waveform completes)
    bool     dvr_cmd_inflight;
    uint32_t dvr_cmd_free_ms;

    // Scheduled hard power cut (KILL#)
    bool     kill_scheduled;
    uint32_t kill_at_ms;

} fsm_t;

static fsm_t g_fsm;

// =============================================================================
// Helpers: action emitters
// =============================================================================

static inline void fsm_emit_action(action_id_t id, uint16_t arg0, uint16_t arg1)
{
    action_t a;
    a.t_enq_ms = millis();
    a.id       = id;
    a.arg0     = arg0;
    a.arg1     = arg1;
    (void)actionq_push(&a);
}

static inline void fsm_emit_beep(beep_pattern_t pat)
{
    fsm_emit_action(ACT_BEEP, (uint16_t)pat, 0);
}

static inline void fsm_emit_led(led_pattern_t pat)
{
    fsm_emit_action(ACT_LED_PATTERN, (uint16_t)pat, 0);
}

static inline void fsm_schedule_kill(uint32_t now_ms, uint32_t delay_ms)
{
    g_fsm.kill_scheduled = true;
    g_fsm.kill_at_ms     = now_ms + delay_ms;
}

static inline void fsm_cancel_kill(void)
{
    g_fsm.kill_scheduled = false;
    g_fsm.kill_at_ms     = 0;
}

// =============================================================================
// Helpers: state transitions
// =============================================================================

static void fsm_enter_state(controller_state_t s, uint32_t now_ms)
{
    g_fsm.state          = s;
    g_fsm.state_entry_ms = now_ms;

    // Default: confirmation inactive unless explicitly armed by transition logic.
    g_fsm.confirm_active       = false;
    g_fsm.confirm_deadline_ms  = 0;
    g_fsm.confirm_expected_a   = DVR_LED_UNKNOWN;
    g_fsm.confirm_expected_b   = DVR_LED_UNKNOWN;

    // Default: do not auto-kill unless states arm it explicitly
    fsm_cancel_kill();

    // UI policy on entry
    switch (s)
    {
        case STATE_OFF:
            fsm_emit_led(LED_OFF);
            // No beep on OFF entry by default (quiet).
            break;

        case STATE_BOOTING:
            // Visual: fast blink while waiting for DVR stable signature
            fsm_emit_led(LED_FAST_BLINK);
            // Audible: short “beep beep” can be done by the caller if desired.
            break;

        case STATE_IDLE:
            fsm_emit_led(LED_SOLID);
            break;

        case STATE_RECORDING:
            // Recording should be quiet; keep minimal UI.
            fsm_emit_led(LED_SLOW_BLINK);
            break;

        case STATE_LOW_BAT:
            fsm_emit_led(LED_SLOW_BLINK);
            fsm_emit_beep(BEEP_LOW_BAT);
            break;

        case STATE_ERROR:
            fsm_emit_led(LED_ERROR_PATTERN);
            fsm_emit_beep(BEEP_ERROR_FAST);
            break;

        case STATE_LOCKOUT:
            fsm_emit_led(LED_LOCKOUT_PATTERN);
            break;

        default:
            // Should never happen; treat as error.
            g_fsm.last_error = ERR_ILLEGAL_STATE;
            fsm_emit_led(LED_ERROR_PATTERN);
            break;
    }
}

// =============================================================================
// Helpers: DVR command guard + confirmation arming
// =============================================================================

static bool fsm_dvr_cmd_allowed(uint32_t now_ms)
{
    if (!g_fsm.dvr_cmd_inflight) return true;
    return (now_ms >= g_fsm.dvr_cmd_free_ms);
}

static void fsm_mark_dvr_cmd(uint32_t now_ms, uint32_t holdoff_ms)
{
    g_fsm.dvr_cmd_inflight = true;
    g_fsm.dvr_cmd_free_ms  = now_ms + holdoff_ms;
}

static void fsm_arm_confirm(uint32_t now_ms,
                            uint32_t timeout_ms,
                            dvr_led_pattern_t a,
                            dvr_led_pattern_t b)
{
    g_fsm.confirm_active      = true;
    g_fsm.confirm_deadline_ms = now_ms + timeout_ms;
    g_fsm.confirm_expected_a  = a;
    g_fsm.confirm_expected_b  = b;
}

// =============================================================================
// High-level behaviours
// =============================================================================

static void fsm_request_power_on(uint32_t now_ms)
{
#if CFG_ENFORCE_BAT_LOCKOUT
    if (g_fsm.lockout_latched)
    {
        fsm_enter_state(STATE_LOCKOUT, now_ms);
        return;
    }
#endif

    if (!fsm_dvr_cmd_allowed(now_ms))
        return;

    // 1) Boot press waveform
    fsm_emit_action(ACT_DVR_PRESS_LONG, 0, 0);
    fsm_mark_dvr_cmd(now_ms, (uint32_t)T_DVR_BOOT_PRESS_MS + (uint32_t)T_DVR_AFTER_PWRON_MS);

    // 2) Enter BOOTING and wait for stable LED signature
    fsm_enter_state(STATE_BOOTING, now_ms);
    fsm_arm_confirm(now_ms, (uint32_t)T_BOOT_TIMEOUT_MS,
                    DVR_LED_SOLID, DVR_LED_SLOW_BLINK);

#if CFG_AUTO_RECORD_ON_BOOT
    // Auto-record is implemented by emitting a short press once allowed.
    // We do that in poll() once dvr_cmd_allowed() becomes true (after AFTER_PWRON).
#endif
}

static void fsm_request_toggle_record(uint32_t now_ms)
{
    if (!fsm_dvr_cmd_allowed(now_ms))
        return;

    fsm_emit_action(ACT_DVR_PRESS_SHORT, 0, 0);
    fsm_mark_dvr_cmd(now_ms, (uint32_t)T_DVR_PRESS_SHORT_MS + (uint32_t)T_DVR_PRESS_GAP_MS);

    // Arm confirmation depending on current state
    if (g_fsm.state == STATE_IDLE)
    {
        // Expect transition to recording (slow blink).
        fsm_arm_confirm(now_ms, (uint32_t)T_BOOT_TIMEOUT_MS, DVR_LED_SLOW_BLINK, DVR_LED_UNKNOWN);
    }
    else if (g_fsm.state == STATE_RECORDING)
    {
        // Expect transition to idle (solid).
        fsm_arm_confirm(now_ms, (uint32_t)T_BOOT_TIMEOUT_MS, DVR_LED_SOLID, DVR_LED_UNKNOWN);
    }
}

static void fsm_request_power_off(uint32_t now_ms)
{
    if (!fsm_dvr_cmd_allowed(now_ms))
        return;

    // Prefer a clean DVR power-off gesture first.
    fsm_emit_action(ACT_DVR_PRESS_LONG, 0, 0);
    fsm_mark_dvr_cmd(now_ms, (uint32_t)T_DVR_PRESS_LONG_MS + (uint32_t)T_DVR_AFTER_PWROFF_MS);

    // Schedule hard power cut after giving the DVR time to shut down.
    // NOTE: If your architecture expects the LTC to cut power immediately, set delay to 0 elsewhere.
    fsm_schedule_kill(now_ms, (uint32_t)T_DVR_AFTER_PWROFF_MS);

    // UI will go quiet / off when we actually die; meanwhile show solid.
    fsm_enter_state(STATE_OFF, now_ms);
}

static void fsm_enter_error(uint32_t now_ms, error_code_t err)
{
    g_fsm.last_error = err;
    fsm_enter_state(STATE_ERROR, now_ms);

#if CFG_AUTO_KILL_ON_ERROR
    // Give the user a short error signal window, then cut power.
    fsm_schedule_kill(now_ms, (uint32_t)T_ERROR_AUTOOFF_MS);
#endif
}

static void fsm_on_battery_update(uint32_t now_ms, battery_state_t bs)
{
    g_fsm.bat_state = bs;

    if (bs == BAT_CRITICAL)
    {
        g_fsm.last_error = ERR_BAT_CRITICAL;
        fsm_enter_state(STATE_LOW_BAT, now_ms);
        // You can decide whether LOW_BAT triggers immediate kill or a graceful stop-first.
        // For now: schedule a hard kill shortly to avoid deep discharge.
        fsm_schedule_kill(now_ms, (uint32_t)T_ERROR_AUTOOFF_MS);
    }
}

static void fsm_on_lockout_enter(uint32_t now_ms)
{
    g_fsm.lockout_latched = true;
    g_fsm.last_error      = ERR_BAT_LOCKOUT;
    fsm_enter_state(STATE_LOCKOUT, now_ms);
}

static void fsm_on_lockout_exit(uint32_t now_ms)
{
    g_fsm.lockout_latched = false;
    // Return to OFF; user must request power-on again.
    fsm_enter_state(STATE_OFF, now_ms);
}

// =============================================================================
// Public API
// =============================================================================

void controller_fsm_init(void)
{
    controller_fsm_reset();
}

void controller_fsm_reset(void)
{
    g_fsm.state            = STATE_OFF;
    g_fsm.bat_state         = BAT_UNKNOWN;
    g_fsm.dvr_led          = DVR_LED_UNKNOWN;
    g_fsm.lockout_latched  = false;
    g_fsm.last_error       = ERR_NONE;

    g_fsm.state_entry_ms    = 0;

    g_fsm.confirm_active    = false;
    g_fsm.confirm_deadline_ms = 0;
    g_fsm.confirm_expected_a = DVR_LED_UNKNOWN;
    g_fsm.confirm_expected_b = DVR_LED_UNKNOWN;

    g_fsm.dvr_cmd_inflight  = false;
    g_fsm.dvr_cmd_free_ms   = 0;

    g_fsm.kill_scheduled    = false;
    g_fsm.kill_at_ms        = 0;

    // Set baseline UI
    fsm_emit_led(LED_OFF);
}

void controller_fsm_on_event(const event_t *e)
{
    if (!e) return;

    const uint32_t now_ms = e->t_ms;

    switch (e->id)
    {
        case EV_BTN_LONG_PRESS:
            if (g_fsm.state == STATE_OFF)
            {
                fsm_request_power_on(now_ms);
            }
            else if (g_fsm.state == STATE_LOCKOUT)
            {
                // Ignore; lockout requires exit event.
            }
            else
            {
                // Any “on” state: long press requests power off.
                // If recording, attempt stop-first by toggling, then power off (grace policy).
                if (g_fsm.state == STATE_RECORDING)
                {
                    // Stop recording, then power off (best-effort).
                    fsm_request_toggle_record(now_ms);
                    // After stop, the next long press could power off; but we also allow immediate off.
                }
                fsm_request_power_off(now_ms);
            }
            break;

        case EV_BTN_SHORT_PRESS:
            if (g_fsm.state == STATE_IDLE || g_fsm.state == STATE_RECORDING)
            {
                fsm_request_toggle_record(now_ms);
            }
            // In OFF/BOOTING/ERROR/LOW_BAT/LOCKOUT: ignore short presses.
            break;

        case EV_DVR_LED_PATTERN_CHANGED:
        {
            const dvr_led_pattern_t p = (dvr_led_pattern_t)(e->arg0 & 0xFF);
            g_fsm.dvr_led = p;

            // If the DVR reports an explicit error signature, escalate quickly.
            if (p == DVR_LED_FAST_BLINK)
            {
                fsm_enter_error(now_ms, ERR_DVR_CARD_ERROR);
                break;
            }
            if (p == DVR_LED_ABNORMAL_BOOT)
            {
                fsm_enter_error(now_ms, ERR_DVR_ABNORMAL_BOOT);
                break;
            }

            // Confirmation logic
            if (g_fsm.confirm_active)
            {
                if (p == g_fsm.confirm_expected_a || p == g_fsm.confirm_expected_b)
                {
                    g_fsm.confirm_active = false;

                    // Resolve to state based on LED
                    if (p == DVR_LED_SLOW_BLINK)
                    {
                        fsm_enter_state(STATE_RECORDING, now_ms);
                        // Confirmation feedback: double beep on recording confirmed
                        fsm_emit_beep(BEEP_DOUBLE);
                    }
                    else if (p == DVR_LED_SOLID)
                    {
                        fsm_enter_state(STATE_IDLE, now_ms);
                        // Optional: single beep on stop confirmed
                        fsm_emit_beep(BEEP_SINGLE);
                    }
                }
            }

            // Passive state tracking (if LED indicates clear mismatch)
            if (!g_fsm.confirm_active)
            {
                if (g_fsm.state == STATE_BOOTING)
                {
                    if (p == DVR_LED_SOLID)      fsm_enter_state(STATE_IDLE, now_ms);
                    if (p == DVR_LED_SLOW_BLINK) fsm_enter_state(STATE_RECORDING, now_ms);
                }
                else if (g_fsm.state == STATE_IDLE && p == DVR_LED_SLOW_BLINK)
                {
                    fsm_enter_state(STATE_RECORDING, now_ms);
                }
                else if (g_fsm.state == STATE_RECORDING && p == DVR_LED_SOLID)
                {
                    fsm_enter_state(STATE_IDLE, now_ms);
                }
            }
        } break;

        case EV_BAT_STATE_CHANGED:
        {
            const battery_state_t bs = (battery_state_t)(e->arg0 & 0xFF);
            fsm_on_battery_update(now_ms, bs);
        } break;

        case EV_BAT_LOCKOUT_ENTER:
            fsm_on_lockout_enter(now_ms);
            break;

        case EV_BAT_LOCKOUT_EXIT:
            fsm_on_lockout_exit(now_ms);
            break;

        // LTC INT events are produced by the power/button path;
        // button module should translate those into EV_BTN_* for FSM.
        case EV_LTC_INT_ASSERTED:
        case EV_LTC_INT_DEASSERTED:
        default:
            // Ignore unknown/unhandled events at FSM layer.
            break;
    }
}

void controller_fsm_poll(uint32_t now_ms)
{
    // Release DVR pacing guard once time has elapsed
    if (g_fsm.dvr_cmd_inflight && now_ms >= g_fsm.dvr_cmd_free_ms)
    {
        g_fsm.dvr_cmd_inflight = false;
    }

    // Boot auto-record policy: after power-on guard opens, issue a toggle
#if CFG_AUTO_RECORD_ON_BOOT
    if (g_fsm.state == STATE_BOOTING)
    {
        // Only attempt if the DVR command guard is free AND we have not yet entered a stable state.
        if (fsm_dvr_cmd_allowed(now_ms))
        {
            // If we haven't already started recording (LED confirm), request it.
            // This is best-effort; LED confirmation will settle the true state.
            fsm_request_toggle_record(now_ms);
        }
    }
#endif

    // Confirmation timeout
    if (g_fsm.confirm_active && now_ms >= g_fsm.confirm_deadline_ms)
    {
        g_fsm.confirm_active = false;

        // In booting, a confirmation timeout is a boot failure.
        if (g_fsm.state == STATE_BOOTING)
        {
            fsm_enter_error(now_ms, ERR_DVR_BOOT_TIMEOUT);
        }
    }

    // Scheduled power cut (KILL#)
    if (g_fsm.kill_scheduled && now_ms >= g_fsm.kill_at_ms)
    {
        g_fsm.kill_scheduled = false;
        fsm_emit_action(ACT_LTC_KILL_ASSERT, 0, 0);
        // After this action, the system is expected to die.
    }
}

controller_state_t controller_fsm_state(void)
{
    return g_fsm.state;
}

error_code_t controller_fsm_error(void)
{
    return g_fsm.last_error;
}

battery_state_t controller_fsm_battery_state(void)
{
    return g_fsm.bat_state;
}

dvr_led_pattern_t controller_fsm_dvr_led_pattern(void)
{
    return g_fsm.dvr_led;
}

bool controller_fsm_lockout(void)
{
    return g_fsm.lockout_latched;
}

