#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * dvr_ctrl.h
 *
 * DVR control module (gesture authority)
 *
 * Responsibilities:
 * - Owns what gesture means what:
 *     - power-on:     long press (boot_press_ms)
 *     - toggle record: short press (press_short_ms)
 *     - power-off:    long press (press_long_ms)
 * - Enforces simple lifecycle legality using an internal "assumed" state.
 * - Does NOT depend on DVR LED classification; the system FSM owns confirmation.
 * - Non-blocking: call dvr_ctrl_tick() frequently from the superloop.
 *
 * Hardware interface expectation:
 * - btn_set(true)  => assert contact closure (press)
 * - btn_set(false) => release contact closure
 *
 * Designed to drive PhotoMOS SSR input (e.g. PD7 -> BTN_DVR), and nothing else.
 *
 * Note:
 * - Internal gesture stepper state is intentionally opaque to callers (kept as uint8_t
 *   in the struct; implementation enum lives in dvr_ctrl.cpp).
 */

typedef enum {
    DVR_CTRL_REQ_ACCEPTED = 0,
    DVR_CTRL_REQ_BUSY,
    DVR_CTRL_REQ_REJECTED
} dvr_ctrl_req_status_t;

typedef struct {
    dvr_ctrl_req_status_t status;
    bool noop;                  // true when request is a no-op due to idempotency
} dvr_ctrl_req_result_t;

/**
 * "Assumed" DVR lifecycle state tracked locally for legality/idempotency.
 * Since LED decode is outside this module, these are assumptions based on
 * accepted commands, not confirmed reality.
 */
typedef enum {
    DVR_CTRL_ASSUME_OFF = 0,
    DVR_CTRL_ASSUME_ON_IDLE,
    DVR_CTRL_ASSUME_ON_RECORDING
} dvr_ctrl_assumed_state_t;

/**
 * Callback used by dvr_ctrl to assert/release the DVR button contact closure.
 * Return true if the output could be set, false if hardware layer refused.
 */
typedef bool (*dvr_ctrl_btn_set_fn)(bool asserted);

/**
 * Optional callback invoked when a gesture completes (press+release done).
 * Useful if you want the top-level FSM to log completion, etc.
 */
typedef void (*dvr_ctrl_gesture_done_fn)(void* user, const char* gesture_name);

typedef struct {
    // Durations (ms)
    uint16_t press_short_ms;     // e.g. 120 ms
    uint16_t press_long_ms;      // e.g. 1500 ms (power on/off)
    uint16_t boot_press_ms;      // if you want boot to differ from generic long
    uint16_t guard_ms;           // minimum time after release before next press
} dvr_ctrl_cfg_t;

typedef struct {
    dvr_ctrl_cfg_t cfg;

    dvr_ctrl_btn_set_fn btn_set;
    dvr_ctrl_gesture_done_fn on_done;
    void* user;

    // --- internal ---
    dvr_ctrl_assumed_state_t assumed;

    bool busy;
    bool btn_asserted;

    uint32_t t_deadline_ms;      // next transition deadline
    uint32_t t_guard_until_ms;   // guard window end

    uint8_t step;                // opaque internal stepper state (see dvr_ctrl.cpp)

    const char* active_gesture;
    uint16_t active_hold_ms;

    // pending "state change" to apply on acceptance
    bool pending_state_update;
    dvr_ctrl_assumed_state_t pending_next_state;
} dvr_ctrl_t;

/**
 * Initialise the controller. Starts in assumed OFF unless you override later.
 */
void dvr_ctrl_init(dvr_ctrl_t* self,
                   const dvr_ctrl_cfg_t* cfg,
                   dvr_ctrl_btn_set_fn btn_set,
                   dvr_ctrl_gesture_done_fn on_done,
                   void* user);

/**
 * Force the assumed state (e.g. after an LED-confirmed truth update).
 * This is how the top-level FSM can reconcile assumptions with reality.
 */
void dvr_ctrl_set_assumed_state(dvr_ctrl_t* self, dvr_ctrl_assumed_state_t st);
dvr_ctrl_assumed_state_t dvr_ctrl_get_assumed_state(const dvr_ctrl_t* self);

bool dvr_ctrl_is_busy(const dvr_ctrl_t* self);

/**
 * Tick function: call often (e.g. each loop) with a monotonic ms timebase.
 */
void dvr_ctrl_tick(dvr_ctrl_t* self, uint32_t now_ms);

/**
 * Idempotent DVR requests.
 * - power_on is a no-op if already ON (idle or recording) -> ACCEPTED + noop=true
 * - toggle_record is rejected if assumed OFF (you have no right to toggle)
 * - power_off is a no-op if already OFF -> ACCEPTED + noop=true
 *
 * These only schedule a gesture; they do not block.
 */
dvr_ctrl_req_result_t dvr_request_power_on(dvr_ctrl_t* self, uint32_t now_ms);
dvr_ctrl_req_result_t dvr_request_toggle_record(dvr_ctrl_t* self, uint32_t now_ms);
dvr_ctrl_req_result_t dvr_request_power_off(dvr_ctrl_t* self, uint32_t now_ms);

/**
 * Emergency stop: releases the button immediately and clears busy state.
 * Does not change assumed state.
 *
 * IMPORTANT:
 * - Implementation should apply guard based on "now_ms".
 *   Recommend updating the implementation signature to:
 *       void dvr_ctrl_abort(dvr_ctrl_t* self, uint32_t now_ms);
 *   If you keep this signature, guard timing in abort() will be less reliable.
 */
void dvr_ctrl_abort(dvr_ctrl_t* self);
