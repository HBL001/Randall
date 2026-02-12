#include <dvr_ctrl.h>
#include <timings.h>

// Internal stepper states (opaque to callers; dvr_ctrl_t stores uint8_t step)
typedef enum {
    DVR_CTRL_STEP_IDLE = 0,
    DVR_CTRL_STEP_ASSERT,
    DVR_CTRL_STEP_HOLD,
    DVR_CTRL_STEP_RELEASE
} dvr_ctrl_step_t;

static dvr_ctrl_req_result_t req_busy(void) {
    dvr_ctrl_req_result_t r;
    r.status = DVR_CTRL_REQ_BUSY;
    r.noop = false;
    return r;
}

static dvr_ctrl_req_result_t req_rejected(void) {
    dvr_ctrl_req_result_t r;
    r.status = DVR_CTRL_REQ_REJECTED;
    r.noop = false;
    return r;
}

static dvr_ctrl_req_result_t req_accepted(bool noop) {
    dvr_ctrl_req_result_t r;
    r.status = DVR_CTRL_REQ_ACCEPTED;
    r.noop = noop;
    return r;
}

static void start_gesture(dvr_ctrl_t* self,
                          uint32_t now_ms,
                          const char* name,
                          uint16_t hold_ms,
                          bool do_state_update,
                          dvr_ctrl_assumed_state_t next_state)
{
    // Guard window is enforced independent of busy state.
    // If we are inside guard, caller gets BUSY (deterministic retry).
    if (now_ms < self->t_guard_until_ms) {
        return;
    }

    self->busy = true;
    self->step = (uint8_t)DVR_CTRL_STEP_ASSERT;
    self->active_gesture = name;
    self->active_hold_ms = hold_ms;

    self->pending_state_update = do_state_update;
    self->pending_next_state = next_state;

    // Immediate action will happen in tick() at or after now_ms
    self->t_deadline_ms = now_ms;
}

void dvr_ctrl_init(dvr_ctrl_t* self,
                   const dvr_ctrl_cfg_t* cfg,
                   dvr_ctrl_btn_set_fn btn_set,
                   dvr_ctrl_gesture_done_fn on_done,
                   void* user)
{
    if (!self) return;

    self->cfg.press_short_ms = cfg ? cfg->press_short_ms : T_DVR_PRESS_SHORT_MS;
    self->cfg.press_long_ms  = cfg ? cfg->press_long_ms  : T_DVR_PRESS_LONG_MS;
    self->cfg.boot_press_ms  = cfg ? cfg->boot_press_ms  : T_DVR_BOOT_PRESS_MS;
    self->cfg.guard_ms       = cfg ? cfg->guard_ms       : T_DVR_PRESS_GAP_MS;

    self->btn_set = btn_set;
    self->on_done = on_done;
    self->user = user;

    self->assumed = DVR_CTRL_ASSUME_OFF;

    self->busy = false;
    self->btn_asserted = false;
    self->t_deadline_ms = 0;
    self->t_guard_until_ms = 0;
    self->step = (uint8_t)DVR_CTRL_STEP_IDLE;
    self->active_gesture = "none";
    self->active_hold_ms = 0;

    self->pending_state_update = false;
    self->pending_next_state = DVR_CTRL_ASSUME_OFF;

    // Ensure released at init
    if (self->btn_set) {
        (void)self->btn_set(false);
    }
}

void dvr_ctrl_set_assumed_state(dvr_ctrl_t* self, dvr_ctrl_assumed_state_t st)
{
    if (!self) return;
    self->assumed = st;
}

dvr_ctrl_assumed_state_t dvr_ctrl_get_assumed_state(const dvr_ctrl_t* self)
{
    return self ? self->assumed : DVR_CTRL_ASSUME_OFF;
}

bool dvr_ctrl_is_busy(const dvr_ctrl_t* self)
{
    return self ? self->busy : false;
}

void dvr_ctrl_abort(dvr_ctrl_t* self)
{
    if (!self) return;

    if (self->btn_set) {
        (void)self->btn_set(false);
    }

    self->btn_asserted = false;
    self->busy = false;
    self->step = (uint8_t)DVR_CTRL_STEP_IDLE;
    self->active_gesture = "aborted";
    self->active_hold_ms = 0;
    self->pending_state_update = false;

    // Conservative guard: block immediate re-press.
    // Without now_ms in signature, use the best proxy we have.
    self->t_guard_until_ms = self->t_deadline_ms + (uint32_t)self->cfg.guard_ms;
    self->t_deadline_ms = self->t_guard_until_ms;
}

void dvr_ctrl_tick(dvr_ctrl_t* self, uint32_t now_ms)
{
    if (!self) return;
    if (!self->busy) return;

    // Wait until deadline for next step
    if (now_ms < self->t_deadline_ms) {
        return;
    }

    switch ((dvr_ctrl_step_t)self->step) {
    case DVR_CTRL_STEP_ASSERT:
        if (self->btn_set) {
            if (!self->btn_set(true)) {
                // Hardware refused -> abort to safe
                dvr_ctrl_abort(self);
                return;
            }
        }
        self->btn_asserted = true;
        self->step = (uint8_t)DVR_CTRL_STEP_HOLD;
        self->t_deadline_ms = now_ms + (uint32_t)self->active_hold_ms;
        break;

    case DVR_CTRL_STEP_HOLD:
        self->step = (uint8_t)DVR_CTRL_STEP_RELEASE;
        self->t_deadline_ms = now_ms;
        break;

    case DVR_CTRL_STEP_RELEASE:
        // Release the button
        if (self->btn_set) {
            (void)self->btn_set(false);
        }
        self->btn_asserted = false;

        // Apply assumed state update at gesture completion (release)
        if (self->pending_state_update) {
            self->assumed = self->pending_next_state;
            self->pending_state_update = false;
        }

        // Arm guard from RELEASE time
        self->t_guard_until_ms = now_ms + (uint32_t)self->cfg.guard_ms;

        // Gesture is now complete at waveform level
        self->busy = false;
        self->step = (uint8_t)DVR_CTRL_STEP_IDLE;

        if (self->on_done) {
            self->on_done(self->user, self->active_gesture);
        }

        self->active_gesture = "none";
        self->active_hold_ms = 0;
        break;

    case DVR_CTRL_STEP_IDLE:
    default:
        // Safety
        dvr_ctrl_abort(self);
        break;
    }
}

dvr_ctrl_req_result_t dvr_request_power_on(dvr_ctrl_t* self, uint32_t now_ms)
{
    if (!self) return req_rejected();

    // If gesture in-flight, busy.
    if (self->busy) return req_busy();

    // Idempotent NOOP must bypass guard
    if (self->assumed == DVR_CTRL_ASSUME_ON_IDLE ||
        self->assumed == DVR_CTRL_ASSUME_ON_RECORDING) {
        return req_accepted(true);
    }

    // Guard applies only to starting a real gesture
    if (now_ms < self->t_guard_until_ms) return req_busy();

    // schedule boot long-press gesture; assumed -> ON_IDLE
    start_gesture(self, now_ms, "power_on", self->cfg.boot_press_ms,
                  true, DVR_CTRL_ASSUME_ON_IDLE);

    // If guard prevented start, treat as busy
    if (!self->busy) return req_busy();

    return req_accepted(false);
}

dvr_ctrl_req_result_t dvr_request_toggle_record(dvr_ctrl_t* self, uint32_t now_ms)
{
    if (!self) return req_rejected();

    if (self->busy) return req_busy();

    // Illegal if assumed OFF (you cannot toggle what you consider off)
    if (self->assumed == DVR_CTRL_ASSUME_OFF) {
        return req_rejected();
    }

    // Guard applies to starting a real gesture
    if (now_ms < self->t_guard_until_ms) return req_busy();

    // toggle short press; flip assumed state
    dvr_ctrl_assumed_state_t next =
        (self->assumed == DVR_CTRL_ASSUME_ON_RECORDING) ?
            DVR_CTRL_ASSUME_ON_IDLE :
            DVR_CTRL_ASSUME_ON_RECORDING;

    start_gesture(self, now_ms, "toggle_record", self->cfg.press_short_ms,
                  true, next);

    if (!self->busy) return req_busy();

    return req_accepted(false);
}

dvr_ctrl_req_result_t dvr_request_power_off(dvr_ctrl_t* self, uint32_t now_ms)
{
    if (!self) return req_rejected();

    if (self->busy) return req_busy();

    // Idempotent NOOP must bypass guard
    if (self->assumed == DVR_CTRL_ASSUME_OFF) {
        return req_accepted(true);
    }

    // Guard applies only to starting a real gesture
    if (now_ms < self->t_guard_until_ms) return req_busy();

    // schedule long-press power-off; assumed -> OFF
    start_gesture(self, now_ms, "power_off", self->cfg.press_long_ms,
                  true, DVR_CTRL_ASSUME_OFF);

    if (!self->busy) return req_busy();

    return req_accepted(false);
}
