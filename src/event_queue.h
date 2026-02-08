// event_queue.h
// Deterministic event ring buffer (ISR-safe) for ATmega328P.
//
// Producer: ISR and/or polling code
// Consumer: main loop (single thread)
//
// Policy: drop-new on full; increment dropped counter.

#pragma once

#include "config.h"
#include <stdint.h>

#ifdef __AVR__
  #include <util/atomic.h>
#endif

// Keep event small and fixed-size.
// args are intentionally generic; you decide meaning per event id.
// Examples:
// - EV_DVR_LED_PATTERN_CHANGED: arg0 = (uint16_t)new_pattern
// - EV_BAT_STATE_CHANGED:       arg0 = (uint16_t)new_bat_state
typedef struct
{
    uint32_t        t_ms;
    event_id_t      id;
    event_source_t  src;
    event_reason_t  reason;
    uint16_t        arg0;
    uint16_t        arg1;
} event_t;

void     eventq_init(void);

// Enqueue from main context (atomic).
bool     eventq_push(const event_t *e);

// Enqueue from ISR context (non-atomic; assumes interrupts already disabled by hardware).
// Safe to call from ISR; does not re-enable interrupts.
bool     eventq_push_isr(const event_t *e);

// Pop one event (atomic). Returns true if one was dequeued.
bool     eventq_pop(event_t *out);

// Number of queued events (approximate but safe).
uint8_t  eventq_count(void);

// How many events were dropped due to full queue.
uint16_t eventq_dropped(void);

// Clear queue + dropped count (atomic)
void     eventq_clear(void);
