// enums.h
#pragma once
#include <stdint.h>

// =============================================================================
// 1) INPUT PLANE: Events and their sources
// =============================================================================

enum event_id_t : uint8_t
{
    EV_NONE = 0,

    // Raw / physical inputs
    EV_LTC_INT_ASSERTED,
    EV_LTC_INT_DEASSERTED,
    EV_BTN_SHORT_PRESS,
    EV_BTN_LONG_PRESS,

    // DVR LED observation (classifier / bridge)
    EV_DVR_LED_PATTERN_CHANGED,   // arg0=dvr_led_pattern_t
    EV_DVR_LED_EDGE_ON,
    EV_DVR_LED_EDGE_OFF,

    // Battery observation
    EV_BAT_STATE_CHANGED,         // arg0=battery_state_t, arg1=adc (if you use it)
    EV_BAT_LOCKOUT_ENTER,
    EV_BAT_LOCKOUT_EXIT,

    // Derived DVR semantic events (from LED / status discriminator)
    EV_DVR_POWERED_ON_IDLE,
    EV_DVR_RECORD_STARTED,
    EV_DVR_RECORD_STOPPED,
    EV_DVR_POWERED_OFF,
    EV_DVR_ERROR                  // arg0=error_code_t, arg1=detail (e.g. last pattern)
};

enum event_source_t : uint8_t
{
    SRC_NONE = 0,
    SRC_LTC,
    SRC_BUTTON,
    SRC_DVR_LED,
    SRC_DVR_STATUS,   // <<< ADDED: derived semantic status from LED patterns
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

enum action_id_t : uint8_t
{
    ACT_NONE = 0,
    ACT_BEEP,               // arg0=beep_pattern_t
    ACT_LED_PATTERN,        // arg0=led_pattern_t
    ACT_DVR_PRESS_SHORT,
    ACT_DVR_PRESS_LONG,
    ACT_LTC_KILL_ASSERT,
    ACT_LTC_KILL_DEASSERT,
    ACT_CLEAR_PENDING,
    ACT_ENTER_LOCKOUT,
    ACT_EXIT_LOCKOUT
};

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
    LED_NONE = 0,
    LED_OFF,
    LED_SOLID,
    LED_SLOW_BLINK,
    LED_FAST_BLINK,
    LED_LOCKOUT_PATTERN,
    LED_ERROR_PATTERN
};

enum result_t : uint8_t
{
    RET_OK = 0,
    RET_WAIT,
    RET_RETRY,
    RET_FAIL
};
