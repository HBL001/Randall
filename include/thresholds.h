// thresholds.h
// Randall Sport Camera Controller - ADC thresholds
// All battery / lockout cut-points live here.

#pragma once

// ADC reference: AVCC (5.0 V)
// Divider: 68k / 33k → 0.3267
//
// IMPORTANT: These are raw ADC counts (0..1023). If you change divider or Vref,
// you MUST re-derive these numbers.

// -----------------------------------------------------------------------------
// Battery gauge thresholds (raw ADC counts)
// -----------------------------------------------------------------------------
#define ADC_FULL             548
#define ADC_HALF             495
#define ADC_LOW              475
#define ADC_CRITICAL         468

// Lockout thresholds (with hysteresis)
#define ADC_LOCKOUT_ENTER    455
#define ADC_LOCKOUT_EXIT     475   // hysteresis
