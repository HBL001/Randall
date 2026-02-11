// enums.h
#pragma once
#include <stdint.h>

// =============================================================================
// 1) INPUT PLANE: Events and their sources
// =============================================================================

enum event_id_t : uint8_t
{
    EV_NONE = 0,
    EV_LTC_INT_ASSERTED,
    EV_LTC_INT_DEASSERTED,
    EV_BTN_SHORT_PRESS,
    EV_BTN_LONG_PRESS,
    EV_DVR_LED_PATTERN_CHANGED,
    EV_BAT_STATE_CHANGED,
    EV_BAT_LOCKOUT_ENTER,
    EV_BAT_LOCKOUT_EXIT,
    EV_DVR_LED_EDGE_ON,
    EV_DVR_LED_EDGE_OFF
};

enum event_source_t : uint8_t
{
    SRC_NONE = 0,
    SRC_LTC,
    SRC_BUTTON,
    SRC_DVR_LED,
    SRC_BATTERY,
    SRC_FSM
};

enum event_reason_t : uint8_t
{
    EVR_NONE = 0,
    EVR_EDGE_RISE,
    EVR_EDGE_FALL,
    EVR_TIMEOUT,
    EVR_CLASSIFIER_STABLE,
    EVR_SAMPLE_PERIODIC,
    EVR_HYSTERESIS,
    EVR_INTERNAL
};

// =============================================================================
// 2) OBSERVATION PLANE: Classifier / interpretations (inputs to FSM)
// =============================================================================

enum dvr_led_pattern_t : uint8_t
{
    DVR_LED_UNKNOWN = 0,
    DVR_LED_OFF,
    DVR_LED_SOLID,          // "ON / IDLE"
    DVR_LED_SLOW_BLINK,     // "RECORDING"
    DVR_LED_FAST_BLINK,     // "ERROR (e.g. card) / update"
    DVR_LED_ABNORMAL_BOOT
};

enum battery_state_t : uint8_t
{
    BAT_UNKNOWN = 0,
    BAT_FULL,
    BAT_HALF,
    BAT_LOW,
    BAT_CRITICAL
};

// =============================================================================
// 3) FSM PLANE: Controller state + transition reasons / errors
// =============================================================================

enum controller_state_t : uint8_t
{
    STATE_OFF = 0,
    STATE_BOOTING,
    STATE_IDLE,
    STATE_RECORDING,
    STATE_LOW_BAT,
    STATE_ERROR,
    STATE_LOCKOUT
};

enum transition_reason_t : uint8_t
{
    TR_NONE = 0,
    TR_USER_REQUEST,
    TR_DVR_CONFIRMED,
    TR_DVR_STOPPED,
    TR_TIMEOUT,
    TR_LOW_BAT,
    TR_LOCKOUT,
    TR_DVR_ERROR,
    TR_INTERNAL_GUARD
};

enum error_code_t : uint8_t
{
    ERR_NONE = 0,
    ERR_DVR_BOOT_TIMEOUT,
    ERR_DVR_ABNORMAL_BOOT,
    ERR_DVR_CARD_ERROR,
    ERR_BAT_CRITICAL,
    ERR_BAT_LOCKOUT,
    ERR_ILLEGAL_STATE,
    ERR_UNEXPECTED_EVENT,
    ERR_UNEXPECTED_LED_PATTERN
};

// =============================================================================
// 4) OUTPUT PLANE: Actions emitted by the FSM (executed by actuator layer)
// =============================================================================
//
// NOTE (Option A canonicalisation):
// - Executor owns ONLY LED + BEEP + LTC kill outputs.
// - DVR gesture control is owned by dvr_ctrl (direct calls), so no ACT_DVR_* here.
//

enum action_id_t : uint8_t
{
    ACT_NONE = 0,
    ACT_BEEP,               // param: beep_pattern_t (arg0)
    ACT_LED_PATTERN,        // param: led_pattern_t  (arg0)
    ACT_LTC_KILL_ASSERT,    // assert KILL# (cut power)
    ACT_LTC_KILL_DEASSERT,  // normally keep deasserted; included for completeness
    ACT_CLEAR_PENDING,      // clear pending flags / reset classifier buffers
    ACT_ENTER_LOCKOUT,      // latch lockout in software
    ACT_EXIT_LOCKOUT
};

// Parameter enums for actions (keeps ACT_BEEP and ACT_LED_PATTERN auditable).
enum beep_pattern_t : uint8_t
{
    BEEP_NONE = 0,
    BEEP_SINGLE,
    BEEP_DOUBLE,
    BEEP_TRIPLE,
    BEEP_ERROR_FAST,
    BEEP_LOW_BAT
};

enum led_pattern_t : uint8_t
{
    // IMPORTANT: keep LED_OFF == 0 to avoid semantic/numeric drift.
    LED_OFF = 0,
    LED_SOLID,
    LED_SLOW_BLINK,
    LED_FAST_BLINK,
    LED_LOCKOUT_PATTERN,
    LED_ERROR_PATTERN
};

// Optional: standardized result codes for pure functions and drivers
enum result_t : uint8_t
{
    RET_OK = 0,
    RET_WAIT,
    RET_RETRY,
    RET_FAIL
};
