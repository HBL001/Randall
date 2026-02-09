// action_queue.h
//
// Producer: FSM (main loop context)
// Consumer: executor (main loop context)
//
// Policy: drop-new on full; increment dropped counter.

#pragma once

#include "config.h"
#include <stdint.h>

#ifdef __AVR__
  #include <util/atomic.h>
#endif

typedef struct
{
    uint32_t    t_enq_ms;   // timestamp at enqueue (for audit)
    action_id_t id;         // ACT_*
    uint16_t    arg0;       // e.g., beep_pattern_t or led_pattern_t
    uint16_t    arg1;       // spare (duration overrides etc if you want later)
} action_t;

void     actionq_init(void);

bool     actionq_push(const action_t *a);
bool     actionq_push_isr(const action_t *a);   // optional; safe if you enqueue from ISR later

bool     actionq_pop(action_t *out);

uint8_t  actionq_count(void);
uint16_t actionq_dropped(void);
void     actionq_clear(void);
