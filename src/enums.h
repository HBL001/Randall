// enums.h
// Randall Sport Camera Controller (ATmega328P / Arduino Nano)
// Canonical enumerations for deterministic event → state → action architecture.
//
// Memory discipline:
// - All enums are explicitly 1 byte (uint8_t) to keep event/action structs small.
//
// Rule of thumb:
// - Inputs produce events (facts).
// - FSM consumes events and updates controller state (pure decision).
// - FSM emits actions (commands) into an action queue.
// - Action executor performs commands non-blocking using timestamps.
//
// updated to use 8 bits bytes to save SRAM
//
#pragma once

#include <stdint.h>

// =============================================================================
// 1) INPUT PLANE: Events and their sources
// =============================================================================

enum event_id_t : uint8_t
{
    EV_NONE = 0,

    // --- Power / LTC2954 ---
    EV_LTC_INT_ASSERTED,          // LTC INT# went active (edge)
    EV_LTC_INT_DEASSERTED,        // LTC INT# returned inactive (edge)

    // --- User button (derived or separate input) ---
    EV_BTN_SHORT_PRESS,
    EV_BTN_LONG_PRESS,

    // --- DVR LED classifier output updates ---
    EV_DVR_LED_PATTERN_CHANGED,   // payload: dvr_led_pattern_t new_pattern (arg0)

    // --- Battery sampling updates ---
    EV_BAT_STATE_CHANGED,         // payload: battery_state_t new_bat_state (arg0)
    EV_BAT_LOCKOUT_ENTER,         // battery unsafe -> refuse operation
    EV_BAT_LOCKOUT_EXIT           // hysteresis cleared
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

// Optional: why an event was raised (audit trail)
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
    DVR_LED_FAST_BLINK,     // "ERROR (e.g. card)"
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

// Transition reason is separate from error_code_t so you can audit normal transitions cleanly.
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

    // DVR-related
    ERR_DVR_BOOT_TIMEOUT,
    ERR_DVR_ABNORMAL_BOOT,
    ERR_DVR_CARD_ERROR,

    // Battery / safety
    ERR_BAT_CRITICAL,
    ERR_BAT_LOCKOUT,

    // Controller / logic
    ERR_ILLEGAL_STATE,
    ERR_UNEXPECTED_EVENT,
    ERR_UNEXPECTED_LED_PATTERN
};

// =============================================================================
// 4) OUTPUT PLANE: Actions emitted by the FSM (executed by actuator layer)
// =============================================================================

// High-level action identifiers (what the executor knows how to do).
enum action_id_t : uint8_t
{
    ACT_NONE = 0,

    // --- Feedback ---
    ACT_BEEP,               // param: beep_pattern_t (arg0)
    ACT_LED_PATTERN,        // param: led_pattern_t (arg0)

    // --- DVR control ---
    ACT_DVR_PRESS_SHORT,    // executor generates non-blocking press waveform
    ACT_DVR_PRESS_LONG,

    // --- Power control ---
    ACT_LTC_KILL_ASSERT,    // assert KILL# (cut power)
    ACT_LTC_KILL_DEASSERT,  // normally keep deasserted; included for completeness

    // --- Housekeeping / safety ---
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
    LED_NONE = 0,
    LED_OFF,
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
