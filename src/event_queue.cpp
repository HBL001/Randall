// event_queue.cpp

#include "event_queue.h"

// NOTE: CFG_EVENT_QUEUE_SIZE must be > 1
static_assert(CFG_EVENT_QUEUE_SIZE > 1, "CFG_EVENT_QUEUE_SIZE must be > 1");

// Ring buffer storage
static volatile uint8_t  s_head = 0;     // write index
static volatile uint8_t  s_tail = 0;     // read index
static volatile uint16_t s_dropped = 0;

static event_t s_buf[CFG_EVENT_QUEUE_SIZE];

// Internal helpers
static inline uint8_t next_index(uint8_t idx)
{
    idx++;
    if (idx >= CFG_EVENT_QUEUE_SIZE) idx = 0;
    return idx;
}

void eventq_init(void)
{
#ifdef __AVR__
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        s_head = 0;
        s_tail = 0;
        s_dropped = 0;
    }
#else
    s_head = 0;
    s_tail = 0;
    s_dropped = 0;
#endif
}

void eventq_clear(void)
{
#ifdef __AVR__
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        s_head = 0;
        s_tail = 0;
        s_dropped = 0;
    }
#else
    s_head = 0;
    s_tail = 0;
    s_dropped = 0;
#endif
}

uint16_t eventq_dropped(void)
{
#ifdef __AVR__
    uint16_t v;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { v = s_dropped; }
    return v;
#else
    return s_dropped;
#endif
}

uint8_t eventq_count(void)
{
#ifdef __AVR__
    uint8_t h, t;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        h = s_head;
        t = s_tail;
    }
#else
    uint8_t h = s_head, t = s_tail;
#endif

    if (h >= t) return (uint8_t)(h - t);
    return (uint8_t)(CFG_EVENT_QUEUE_SIZE - (t - h));
}

// Core enqueue that assumes interrupts are already disabled (safe for ISR).
static inline bool push_core(const event_t *e)
{
    const uint8_t h = s_head;
    const uint8_t n = next_index(h);

    // Full if next head would collide with tail.
    if (n == s_tail)
    {
        s_dropped++;
        return false;
    }

    // Copy event into slot then publish head.
    s_buf[h] = *e;
    s_head = n;
    return true;
}

bool eventq_push_isr(const event_t *e)
{
    // In AVR ISR context, global interrupts are already disabled.
    return push_core(e);
}

bool eventq_push(const event_t *e)
{
#ifdef __AVR__
    bool ok;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        ok = push_core(e);
    }
    return ok;
#else
    return push_core(e);
#endif
}

bool eventq_pop(event_t *out)
{
#ifdef __AVR__
    bool ok = false;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        if (s_tail == s_head)
        {
            ok = false; // empty
        }
        else
        {
            const uint8_t t = s_tail;
            *out = s_buf[t];
            s_tail = next_index(t);
            ok = true;
        }
    }
    return ok;
#else
    if (s_tail == s_head) return false;
    const uint8_t t = s_tail;
    *out = s_buf[t];
    s_tail = next_index(t);
    return true;
#endif
}
