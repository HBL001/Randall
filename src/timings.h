// timings.h
// Randall Sport Camera Controller - Timing constants (ms)
// Keep all classifier / UX timing knobs here so the logic stays clean.

#pragma once

// -----------------------------------------------------------------------------
// DVR LED pattern decode timing windows
// (These are the locked numeric boundaries you gave.)
// -----------------------------------------------------------------------------

// SOLID (idle/on)
#define T_SOLID_MS               1500

// SLOW blink (recording)
#define T_SLOW_MIN_MS             700
#define T_SLOW_MAX_MS            1800
#define T_SLOW_EDGE_MIN_MS        150
#define T_SLOW_EDGE_MAX_MS       1200

// FAST blink (error)
#define T_FAST_MIN_MS              80
#define T_FAST_MAX_MS             450
#define T_FAST_EDGE_MIN_MS         20
#define T_FAST_EDGE_MAX_MS        250

// Abnormal boot signature window
#define T_BOOT_WINDOW_MS         6000
#define T_ABN_SLOW_MIN_MS        1200
#define T_ABN_SLOW_MAX_MS        3200
#define T_ABN_OFF_MIN_MS          800

// -----------------------------------------------------------------------------
// DVR button press waveform timing
// -----------------------------------------------------------------------------
#define T_DVR_PRESS_SHORT_MS    120     // Short press emulation for DVR UI 
#define T_DVR_PRESS_LONG_MS    1500     // Long press emulation 
#define T_DVR_PRESS_GAP_MS      200     // Mandatory dead-time after releasing DVR button


// -----------------------------------------------------------------------------
// Optional UX / debounce timing knobs (start as sane defaults)
// Adjust later if you want “feel” changes without touching state logic.
// -----------------------------------------------------------------------------

// Button press classification (LTC2954)
#define T_BTN_DEBOUNCE_MS            35   // Ignore noise / bounce
#define T_BTN_SHORT_MIN_MS           50   // Minimum valid press (UI tap)
#define T_BTN_WAKE_MIN_MS           350   // Minimum press required to wake / re-enable power path (ONT ≈300 ms)
#define T_BTN_GRACE_MS              500   // Graceful shutdown request.  Stop recording and switch off.
#define T_BTN_NUCLEAR_MS           1500   // Just power off now - hardware will kick in and shutdown by force

// Buzzer patterns (example placeholders)
#define T_BEEP_MS                  80
#define T_BEEP_GAP_MS              80
#define T_DOUBLE_BEEP_GAP_MS      180

// State timeouts (placeholders; tune with real DVR behaviour)
#define T_BOOT_TIMEOUT_MS         8000   // give DVR time to reach a stable LED signature
#define T_ERROR_AUTOOFF_MS        2500   // how long we signal error before cutting power
