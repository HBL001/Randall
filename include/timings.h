// -----------------------------------------------------------------------------
// UX / debounce timing knobs
// Keep these as INPUT-side thresholds and UX pacing values.
// These do NOT define actuator waveforms (see DVR shutter timing section).
// -----------------------------------------------------------------------------

// Button press classification (LTC2954 INT# semantics)  [INPUT SIDE]
#define T_BTN_DEBOUNCE_MS            35    // Ignore noise / bounce
#define T_BTN_SHORT_MIN_MS           50    // Minimum valid UI tap
#define T_BTN_WAKE_MIN_MS           350    // Minimum press required to wake / re-enable power path (ONT ≈300 ms)

// VALIDATED ON HARDWARE: user "grace" hold triggers software shutdown path
#define T_BTN_GRACE_MS              500    // Graceful shutdown request (stop recording, then power off)

// VALIDATED ON HARDWARE (hardware-enforced): beyond this, LTC2954 "nuclear" path will cut power
#define T_BTN_NUCLEAR_MS           1500    // Forced power cut (LTC hardware will win)

// -----------------------------------------------------------------------------
// Feedback timing (executor patterns)
// -----------------------------------------------------------------------------
#define T_BEEP_MS                   80
#define T_BEEP_GAP_MS               80
#define T_DOUBLE_BEEP_GAP_MS       180

// -----------------------------------------------------------------------------
// State timeouts (FSM pacing; tune with real DVR behaviour)
// -----------------------------------------------------------------------------
#define T_BOOT_TIMEOUT_MS          8000    // Time allowed for DVR to reach stable LED signature
#define T_ERROR_AUTOOFF_MS         2500    // Time we signal error before cutting power / returning OFF

// -----------------------------------------------------------------------------
// DVR shutter emulation timing (executor waveform)  [OUTPUT SIDE]
// -----------------------------------------------------------------------------
#define T_DVR_PRESS_SHORT_MS        500
#define T_DVR_PRESS_LONG_MS         3000
#define T_DVR_PRESS_GAP_MS          500
#define T_DVR_AFTER_PWRON_MS        2500
#define T_DVR_AFTER_PWROFF_MS       2500
#define T_DVR_BOOT_PRESS_MS         3000 

// -----------------------------------------------------------------------------
// DVR LED timing classifier thresholds  [INPUT SIDE]
// -----------------------------------------------------------------------------

// SOLID: LED continuously ON (or OFF) with no edges
#define T_SOLID_MS               1500    // ms without change = solid

// SLOW blink (normal recording)
#define T_SLOW_MIN_MS             700    // minimum full period (ON→ON or OFF→OFF)
#define T_SLOW_MAX_MS            1800    // maximum full period
#define T_SLOW_EDGE_MIN_MS        150    // minimum ON or OFF time
#define T_SLOW_EDGE_MAX_MS       1200    // maximum ON or OFF time

// FAST blink (error / fault)
#define T_FAST_MIN_MS              80    // minimum full period
#define T_FAST_MAX_MS             450    // maximum full period
#define T_FAST_EDGE_MIN_MS         20    // minimum ON or OFF time
#define T_FAST_EDGE_MAX_MS        250    // maximum ON or OFF time

// -----------------------------------------------------------------------------
// Abnormal boot signature (reserved / documented behaviour)
// -----------------------------------------------------------------------------
#define T_ABN_SLOW_MIN_MS        1200
#define T_ABN_SLOW_MAX_MS        3200
#define T_ABN_OFF_MIN_MS          800
#define T_ABN_BLINK_BURST_MS     2000   // manual: slow blink for ~2 seconds
#define T_ABN_SHUTOFF_MS         5000   // manual: shuts off after ~5 seconds
