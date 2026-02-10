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
