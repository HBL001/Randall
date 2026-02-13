// config.h
// Randall Sport Camera Controller


#pragma once

// =============================================================================
// Build configuration
// =============================================================================

// Enable serial debug output (comment out for final builds)
#define CFG_DEBUG_SERIAL          1

// Enable audit / trace buffer (event + transition logging)
// Costs RAM; disable for production if needed
#define CFG_ENABLE_TRACE          1

// Maximum sizes (tune explicitly, never implicitly)
#define CFG_EVENT_QUEUE_SIZE      16
#define CFG_ACTION_QUEUE_SIZE     8
#define CFG_TRACE_BUFFER_SIZE     32

// =============================================================================
// Timing base
// =============================================================================

// Master time source:
// 0 = millis()
// 1 = Timer1 1 kHz ISR (future option)
#define CFG_TIMEBASE_MILLIS       0

// Polling cadences (ms)
#define CFG_BATTERY_SAMPLE_MS     250
#define CFG_LED_CLASSIFIER_MS     10
#define CFG_EXECUTOR_TICK_MS      1

// =============================================================================
// Safety / behaviour policy
// =============================================================================

// Refuse power-on when battery is in lockout
#define CFG_ENFORCE_BAT_LOCKOUT   1

// Auto power-off on DVR error
#define CFG_AUTO_KILL_ON_ERROR    1

// Allow DVR auto-record on boot
#define CFG_AUTO_RECORD_ON_BOOT  1

// =============================================================================
// Includes: canonical system vocabulary
// =============================================================================

#include <Arduino.h>

// Hardware + tuning
#include "pins.h"
#include "timings.h"
#include "thresholds.h"

// System vocabulary
#include "enums.h"

// =============================================================================
// Static assertions / sanity guards (compile-time)
// =============================================================================

// Ensure queue sizes are sane for ATmega328P RAM
#if CFG_EVENT_QUEUE_SIZE > 32
  #error "Event queue too large for ATmega328P"
#endif

#if CFG_ACTION_QUEUE_SIZE > 16
  #error "Action queue too large for ATmega328P"
#endif

// =============================================================================
// Event metadata configuration
// =============================================================================

// event_t includes source and reason fields
#define CFG_EVENT_HAS_SRC        1
#define CFG_EVENT_HAS_REASON     1
