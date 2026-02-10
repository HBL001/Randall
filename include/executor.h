// executor.h

#pragma once

#include <stdint.h>
#include "config.h"
#include "pins.h"
#include "action_queue.h"
#include "enums.h"  

void executor_init(void);
void executor_poll(uint32_t now_ms);
void executor_abort_feedback(void);
bool executor_busy(void);
