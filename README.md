# Randall Sport Camera — Optimisation & Cleanup Checklist

*Based on full source review of v8 codebase. Items are grouped by theme and ordered by priority within each group.*

---

## 🔴 CRITICAL — Functional Bugs (fix before any hardware testing)

### 1. Battery events consumed before the FSM sees them
**File:** `main.cpp` — `battery_event_log_poll()`  
**Problem:** This debug logger is called *before* `controller_fsm_poll()` in the loop. It pops and discards `EV_BAT_STATE_CHANGED`, `EV_BAT_LOCKOUT_ENTER`, `EV_BAT_LOCKOUT_EXIT` events. The FSM never receives them — so battery critical state and lockout are effectively dead.  
**Fix:** Delete `battery_event_log_poll()` entirely. Read battery state directly from `drv_fuel_gauge_last_state()` and `drv_fuel_gauge_lockout_active()` for logging — no queue involvement needed.

### 2. DVR LED polarity hardcoded — ignores `pins.h`
**File:** `dvr_led.cpp` — multiple locations  
**Problem:** `LOW` is hardcoded as "LED ON" throughout, but `pins.h` already defines `DVR_STAT_ACTIVE_LEVEL` and `DVR_STAT_INACTIVE_LEVEL`. If your NPN sniffer is inverted (or you ever swap hardware), every pattern classification silently becomes wrong.  
**Fix:** Define `#define DVR_LED_ON_LEVEL DVR_STAT_ACTIVE_LEVEL` at the top of `dvr_led.cpp` and replace all `== LOW` / `== HIGH` checks with `== DVR_LED_ON_LEVEL` / `!= DVR_LED_ON_LEVEL`. Touch lines: 204, 212, 290, 301.

### 3. Boot timeout fires before queue is drained
**File:** `controller_fsm.cpp` — `controller_fsm_poll()`  
**Problem:** The boot timeout check runs at the top of the function, before `eventq_pop()`. If `EV_DVR_POWERED_ON_IDLE` is already sitting in the queue at the exact deadline millisecond, the system incorrectly enters `STATE_ERROR`.  
**Fix:** Move the boot timeout check to *after* the `while (eventq_pop())` loop.

---

## 🟠 HIGH — RAM (critical on 2KB ATmega328P)

### 4. Three large stack allocations — up to 544 bytes simultaneously
**Files:** `drv_dvr_status.cpp`, `main.cpp`, `executor.cpp`  
**Problem:** Three separate `event_t stash[16]` / `action_t stash[16]` arrays are allocated on the stack inside functions that can all be on the call stack in the same loop iteration:
- `poll_led_pattern_events()` — 192 bytes (12 × 16)
- `battery_event_log_poll()` — 192 bytes (already removed by fix #1)
- `executor_poll()` — 160 bytes (10 × 16)

**Fix:** Declare stash arrays as `static` locals. They become BSS globals instead of stack allocations. No functional change.

### 5. `event_t` is 12 bytes — debug fields bloat all queues
**File:** `event_queue.h`  
**Problem:** `event_t` contains `t_ms` (4 bytes), `src` (1 byte), `reason` (1 byte) which are used only for debugging/tracing. At 16 entries, the event queue alone costs 192 bytes.  
**Fix:** Wrap `t_ms`, `src`, and `reason` in `#if CFG_ENABLE_TRACE`. In production builds (`CFG_ENABLE_TRACE 0`) the struct shrinks to 5 bytes, reducing the queue to 80 bytes — saving **112 bytes of RAM**.

### 6. `action_t` carries a 4-byte timestamp — audit only
**File:** `action_queue.h`  
**Problem:** `t_enq_ms` is only used for audit/trace. Costs 4 bytes per entry × 8 entries = 32 bytes.  
**Fix:** Same pattern as above — wrap in `#if CFG_ENABLE_TRACE`.

### 7. ISR edge ring buffer oversized
**File:** `dvr_led.cpp`  
**Problem:** `QN = 32` gives a 160-byte ISR buffer (`uint32_t[32]` + `uint8_t[32]`). At your 10ms poll rate, you cannot accumulate more than ~5 edges between polls even at fast-blink speeds.  
**Fix:** Reduce `QN` to 8. Saves **120 bytes of RAM**. Add a compile-time assert: `static_assert((QN & (QN-1)) == 0, "QN must be power of 2");`

---

## 🟡 MEDIUM — Code Smells & Duplication

### 8. `time_reached()` defined in 4 separate files
**Files:** `controller_fsm.cpp`, `drv_dvr_led.cpp`, `drv_dvr_status.cpp`, `main.cpp`  
**Fix:** Move once to a new `utils.h` header:
```c
static inline bool time_reached(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}
```
Delete the 4 local copies.

### 9. `emit_event` / `emit_action` helpers duplicated across 6 files
**Files:** `ui_policy.cpp`, `controller_fsm.cpp`, `dvr_button.cpp`, `drv_dvr_status.cpp`, `drv_dvr_led.cpp`, `drv_fuel_gauge.cpp`  
**Problem:** Each module has its own local variant of the same 8-line push-to-queue helper.  
**Fix:** Add thin inline helpers to `event_queue.h` and `action_queue.h` respectively (they already know the struct layout). Eliminates 6 copies.

### 10. `drv_dvr_status` uses `SRC_FSM` instead of `SRC_DVR_STATUS`
**File:** `drv_dvr_status.cpp` line 65  
**Fix:** `e.src = SRC_DVR_STATUS;` — one character change but breaks traceability otherwise.

### 11. `drv_dvr_status` — stash/repush has a silent-drop bug
**File:** `drv_dvr_status.cpp` — `poll_led_pattern_events()`  
**Problem:** When the stash fills (`n >= STASH_MAX`), the code does `break`. This exits the drain loop, leaving further events in the queue — but the item that triggered overflow has already been popped and is not in the stash. It's dropped silently with no warning.  
**Fix 1 (quick):** Change `break` to `stash[STASH_MAX-1] = ev;` to at least preserve the last item, or add a `Serial.println(F("WARN: stash overflow"))` before break.  
**Fix 2 (proper, see item #14):** Eliminate this entire pattern.

### 12. Dead `#define` guards in `drv_dvr_led.cpp`
**File:** `drv_dvr_led.cpp` lines 48–54  
**Problem:** `CFG_EVENT_HAS_SRC` and `CFG_EVENT_HAS_REASON` are defined to `0` and then *never used* in any `#if` block. They're commented intent that was never implemented.  
**Fix:** Delete the 6 lines. If you ever want conditional event fields, implement it properly via `#if CFG_ENABLE_TRACE` in `event_queue.h` (see item #5).

### 13. `LED_LOCKOUT_PATTERN` and `LED_ERROR_PATTERN` are visually identical
**File:** `executor.cpp` — `led_step()`  
**Problem:** Both patterns fall through to the same fast-blink code. The enum implies they're distinct, but the executor can't tell them apart.  
**Fix:** Either give them distinct timing (e.g. error = 3 rapid blinks + pause), or merge the enum values if you deliberately want them the same.

---

## 🟢 LOW — Architecture / Future-Proofing

### 14. Eliminate stash/repush from `drv_dvr_status` entirely
**File:** `drv_dvr_status.cpp`  
**Problem:** The whole pop/stash/repush pattern exists because `drv_dvr_status` consumes `EV_DVR_LED_PATTERN_CHANGED` from the shared queue. But `drv_dvr_led` already stores the last pattern internally (`drv_dvr_led_last_pattern()`).  
**Fix:** Delete `poll_led_pattern_events()`. In `drv_dvr_status_poll()`, call `drv_dvr_led_last_pattern()` directly and detect changes locally with a `static dvr_led_pattern_t s_prev`. This removes the need to drain/repush the queue at all, saves the 192-byte stack stash, and eliminates the drop bug.

### 15. Delete (or archive) `dvr_ctrl.*`
**Files:** `src/dvr_ctrl.cpp`, `include/dvr_ctrl.h`  
**Problem:** This module is fully implemented but referenced nowhere in the build. It adds ~500 bytes of Flash and creates confusion about the actual button-press architecture.  
**Fix:** Move to `WIP/` or add `; build_src_filter = -<dvr_ctrl.cpp>` in `platformio.ini`.

### 16. Unimplemented action IDs will requeue forever if ever emitted
**File:** `executor.cpp`, `enums.h`  
**Problem:** `ACT_LTC_KILL_ASSERT`, `ACT_LTC_KILL_DEASSERT`, `ACT_CLEAR_PENDING`, `ACT_ENTER_LOCKOUT`, `ACT_EXIT_LOCKOUT` are in the enum but not handled by the executor. If the FSM ever emits one, it will be stashed and re-queued infinitely.  
**Fix:** Either implement them (KILL especially should be wired — it's your hardware power cut), or add an `#error` compile guard that fires if anything tries to emit them. At minimum, add a `default: /* warn and discard */` path that doesn't stash.

### 17. Queue size should be power-of-two and use mask-based wrapping
**Files:** `event_queue.cpp`, `action_queue.cpp`  
**Problem:** Both use `if (idx >= SIZE) idx = 0;` wrapping. With power-of-two sizes (already 16 and 8), this can be `idx = (idx + 1) & (SIZE - 1)` — branchless and slightly faster on AVR.  
**Fix:** Add `static_assert((CFG_EVENT_QUEUE_SIZE & (CFG_EVENT_QUEUE_SIZE-1)) == 0)` and `static_assert((CFG_ACTION_QUEUE_SIZE & (CFG_ACTION_QUEUE_SIZE-1)) == 0)` to config.h, then switch to mask wrapping.

### 18. `digitalRead()` in hot paths — use direct port reads
**Files:** `dvr_led.cpp` (poll), `dvr_button.cpp`  
**Problem:** `digitalRead()` adds ~50 clock cycles of overhead per call from the Arduino wrapper. Both files already have `DVR_STAT_READ()` and `LTC_INT_READ()` macros defined in `pins.h`.  
**Fix:** Replace `digitalRead(PIN_DVR_STAT)` with `(DVR_STAT_READ() ? HIGH : LOW)` and `digitalRead(PIN_LTC_INT_N)` with `(LTC_INT_READ() ? HIGH : LOW)`.

### 19. `dvr_ctrl_abort()` has no `now_ms` parameter — guard timing is unreliable
**File:** `dvr_ctrl.cpp` / `dvr_ctrl.h`  
**Problem:** The abort function sets `t_guard_until_ms` based on `t_deadline_ms` as a proxy for "now", which may be stale by seconds.  
**Fix:** Add `uint32_t now_ms` parameter. Update the single call site. (Pairs with item #15 — if you delete `dvr_ctrl` this is moot.)

---

## 📋 Summary Table

| # | Item | Files | Impact | Effort |
|---|------|-------|--------|--------|
| 1 | Battery events eaten before FSM | main.cpp | 🔴 Functional bug | 5 min |
| 2 | LED polarity hardcoded | dvr_led.cpp | 🔴 Silent wrong classify | 15 min |
| 3 | Boot timeout before queue drain | controller_fsm.cpp | 🔴 False boot error | 5 min |
| 4 | Stack stash arrays → static | drv_dvr_status, executor | ~350 bytes stack freed | 5 min |
| 5 | Slim event_t under TRACE guard | event_queue.h | ~112 bytes RAM | 30 min |
| 6 | Slim action_t under TRACE guard | action_queue.h | ~32 bytes RAM | 10 min |
| 7 | ISR ring buffer QN: 32→8 | dvr_led.cpp | ~120 bytes RAM | 2 min |
| 8 | Deduplicate time_reached | 4 files → utils.h | Code smell | 10 min |
| 9 | Deduplicate emit helpers | 6 files | Code smell | 20 min |
| 10 | Fix SRC_FSM → SRC_DVR_STATUS | drv_dvr_status.cpp | Traceability | 1 min |
| 11 | Stash overflow silent drop | drv_dvr_status.cpp | Latent bug | 5 min |
| 12 | Delete dead CFG_EVENT_HAS_* | drv_dvr_led.cpp | Dead code | 1 min |
| 13 | Distinguish LOCKOUT/ERROR LED | executor.cpp | UX clarity | 10 min |
| 14 | Replace stash/repush in dvr_status | drv_dvr_status.cpp | Architecture | 20 min |
| 15 | Delete/archive dvr_ctrl.* | src/, include/ | ~500 bytes Flash | 2 min |
| 16 | Unimplemented action IDs | executor.cpp, enums.h | Latent infinite loop | 15 min |
| 17 | Power-of-2 mask wrapping | event_queue, action_queue | Minor perf + asserts | 15 min |
| 18 | Port macros vs digitalRead | dvr_led.cpp, dvr_button.cpp | ~100 cycles/loop | 10 min |
| 19 | dvr_ctrl_abort needs now_ms | dvr_ctrl.cpp | Guard reliability | 5 min |

---

*Total estimated RAM saving from items 4+5+6+7: approximately **364 bytes** on a chip with 2048 bytes total.*
