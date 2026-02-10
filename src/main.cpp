/*
 * test_dvr_ctrl_smoke.cpp  (Arduino / PlatformIO)
 *
 * Purpose: Smoke-test dvr_ctrl gesture scheduler on real hardware with Serial
 * and a real GPIO output for the PhotoMOS SSR drive (D7 / PD7).
 *
 * What it verifies (minimum):
 *  - init releases the button output
 *  - long press waveform occurs once with correct hold time
 *  - short press waveform occurs once with correct hold time
 *  - BUSY / REJECT / NOOP logic behaves
 *  - guard window blocks immediate re-press
 *
 * Wiring:
 *  - Leave your real SSR connected if you want, but safest is to scope D7 first.
 *
 * Build:
 *  - Put this file in src/ and set it as the main sketch (or temporarily replace main.cpp).
 */

#include <Arduino.h>

// -----------------------------
// Pin mapping (matches your architecture)
// -----------------------------
#ifndef PIN_DVR_BTN_CMD
#define PIN_DVR_BTN_CMD 7   // Arduino D7 == PD7
#endif

// -----------------------------
// Simple assert helpers
// -----------------------------
static uint16_t g_fail_count = 0;

static void test_fail(const __FlashStringHelper* msg) {
  g_fail_count++;
  Serial.print(F("FAIL: "));
  Serial.println(msg);
}

static void test_pass(const __FlashStringHelper* msg) {
  Serial.print(F("PASS: "));
  Serial.println(msg);
}

static void expect_true(bool cond, const __FlashStringHelper* msg) {
  if (!cond) test_fail(msg);
  else test_pass(msg);
}

// -----------------------------
// Capture waveform events
// -----------------------------
struct EdgeLog {
  uint32_t t_ms;
  bool asserted;
};

static EdgeLog g_edges[16];
static uint8_t g_edge_count = 0;
static bool g_last_asserted = false;

static void log_edge(uint32_t now_ms, bool asserted) {
  if (g_edge_count < (uint8_t)(sizeof(g_edges) / sizeof(g_edges[0]))) {
    g_edges[g_edge_count++] = { now_ms, asserted };
  }
  Serial.print(F("EDGE t="));
  Serial.print(now_ms);
  Serial.print(F("ms  asserted="));
  Serial.println(asserted ? F("1") : F("0"));
}

static void reset_edge_log(void) {
  g_edge_count = 0;
}

// -----------------------------
// dvr_ctrl callbacks
// -----------------------------
static bool btn_set_cb(bool asserted) {
  uint32_t now_ms = millis();

  // Drive actual pin
  digitalWrite(PIN_DVR_BTN_CMD, asserted ? HIGH : LOW);

  // Log only on transitions (so we can measure hold)
  if (asserted != g_last_asserted) {
    g_last_asserted = asserted;
    log_edge(now_ms, asserted);
  }

  return true; // hardware accepted
}

static void on_done_cb(void* /*user*/, const char* gesture_name) {
  Serial.print(F("DONE: "));
  Serial.println(gesture_name ? gesture_name : "null");
}

// -----------------------------
// Test harness utils
// -----------------------------
static dvr_ctrl_t g_ctrl;

static bool run_until_idle(uint32_t timeout_ms) {
  uint32_t start = millis();
  while ((millis() - start) < timeout_ms) {
    dvr_ctrl_tick(&g_ctrl, millis());
    if (!dvr_ctrl_is_busy(&g_ctrl)) {
      return true;
    }
    // keep loop responsive
    delay(1);
  }
  return false;
}

static int32_t measured_hold_ms(void) {
  // Expect one assert edge then one release edge.
  // Log contains transitions only; so for a gesture we want:
  //   edges[0] asserted=1
  //   edges[1] asserted=0
  if (g_edge_count < 2) return -1;
  if (!g_edges[0].asserted) return -2;
  if (g_edges[1].asserted)  return -3;
  return (int32_t)(g_edges[1].t_ms - g_edges[0].t_ms);
}

static void print_req(const char* name, dvr_ctrl_req_result_t r) {
  Serial.print(name);
  Serial.print(F(": status="));
  Serial.print((int)r.status);
  Serial.print(F(" noop="));
  Serial.println(r.noop ? F("1") : F("0"));
}

// -----------------------------
// Smoke tests
// -----------------------------
static void test_1_init_releases(void) {
  Serial.println(F("\n=== TEST 1: init releases output ==="));

  // Ensure pin is configured
  pinMode(PIN_DVR_BTN_CMD, OUTPUT);
  digitalWrite(PIN_DVR_BTN_CMD, LOW);
  g_last_asserted = false;
  reset_edge_log();

  // Use canonical timings as defaults; allow overrides later if desired
  dvr_ctrl_cfg_t cfg = {};
  cfg.press_short_ms = T_DVR_PRESS_SHORT_MS;
  cfg.press_long_ms  = T_DVR_PRESS_LONG_MS;
  cfg.boot_press_ms  = T_DVR_PRESS_LONG_MS;     // boot == long press (your current policy)
  cfg.guard_ms       = T_DVR_PRESS_GAP_MS;      // guard == inter-press gap

  dvr_ctrl_init(&g_ctrl, &cfg, btn_set_cb, on_done_cb, nullptr);

  expect_true(!dvr_ctrl_is_busy(&g_ctrl), F("ctrl not busy after init"));
  expect_true(digitalRead(PIN_DVR_BTN_CMD) == LOW, F("DVR button output released (LOW)"));
}

static void test_2_illegal_toggle_rejected(void) {
  Serial.println(F("\n=== TEST 2: toggle rejected when assumed OFF ==="));
  dvr_ctrl_set_assumed_state(&g_ctrl, DVR_CTRL_ASSUME_OFF);

  dvr_ctrl_req_result_t r = dvr_request_toggle_record(&g_ctrl, millis());
  print_req("toggle_record", r);

  expect_true(r.status == DVR_CTRL_REQ_REJECTED, F("toggle rejected when OFF"));
  expect_true(!dvr_ctrl_is_busy(&g_ctrl), F("still not busy after rejected request"));
}

static void test_3_power_on_long_press_waveform(void) {
  Serial.println(F("\n=== TEST 3: power_on generates one long press ==="));

  dvr_ctrl_set_assumed_state(&g_ctrl, DVR_CTRL_ASSUME_OFF);
  reset_edge_log();

  uint32_t now = millis();
  dvr_ctrl_req_result_t r = dvr_request_power_on(&g_ctrl, now);
  print_req("power_on", r);

  expect_true(r.status == DVR_CTRL_REQ_ACCEPTED, F("power_on accepted"));
  expect_true(!r.noop, F("power_on not a noop"));
  expect_true(dvr_ctrl_is_busy(&g_ctrl), F("ctrl busy after accepted power_on"));

  bool ok = run_until_idle((uint32_t)T_DVR_PRESS_LONG_MS + (uint32_t)T_DVR_PRESS_GAP_MS + 1500u);
  expect_true(ok, F("gesture completes within timeout"));

  // Basic edge sanity
  expect_true(g_edge_count >= 2, F("observed at least assert+release edges"));

  int32_t hold = measured_hold_ms();
  Serial.print(F("Measured hold="));
  Serial.print(hold);
  Serial.println(F("ms"));

  // Allow modest tolerance due to millis granularity / scheduling
  const int32_t exp = (int32_t)T_DVR_PRESS_LONG_MS;
  const int32_t tol = 60;  // ~1-2 ticks margin + loop jitter
  expect_true(hold >= (exp - tol) && hold <= (exp + tol), F("long press hold within tolerance"));

  // Assumed state should have updated to ON_IDLE on completion
  expect_true(dvr_ctrl_get_assumed_state(&g_ctrl) == DVR_CTRL_ASSUME_ON_IDLE, F("assumed state -> ON_IDLE"));
}

static void test_4_power_on_noop(void) {
  Serial.println(F("\n=== TEST 4: power_on becomes NOOP when already ON ==="));

  // Already ON_IDLE from previous test
  dvr_ctrl_req_result_t r = dvr_request_power_on(&g_ctrl, millis());
  print_req("power_on(again)", r);

  expect_true(r.status == DVR_CTRL_REQ_ACCEPTED, F("power_on accepted"));
  expect_true(r.noop, F("power_on noop when already on"));
  expect_true(!dvr_ctrl_is_busy(&g_ctrl), F("noop does not schedule a gesture"));
}

static void test_5_toggle_short_press_and_guard(void) {
  Serial.println(F("\n=== TEST 5: toggle_record short press + guard enforcement ==="));

  // Ensure ON_IDLE
  dvr_ctrl_set_assumed_state(&g_ctrl, DVR_CTRL_ASSUME_ON_IDLE);

  // Start toggle
  reset_edge_log();
  dvr_ctrl_req_result_t r = dvr_request_toggle_record(&g_ctrl, millis());
  print_req("toggle_record", r);

  expect_true(r.status == DVR_CTRL_REQ_ACCEPTED, F("toggle accepted"));
  expect_true(dvr_ctrl_is_busy(&g_ctrl), F("ctrl busy after toggle"));

  // While busy, a second request should be BUSY
  dvr_ctrl_req_result_t r2 = dvr_request_power_off(&g_ctrl, millis());
  print_req("power_off(while busy)", r2);
  expect_true(r2.status == DVR_CTRL_REQ_BUSY, F("request while busy returns BUSY"));

  bool ok = run_until_idle((uint32_t)T_DVR_PRESS_SHORT_MS + (uint32_t)T_DVR_PRESS_GAP_MS + 1500u);
  expect_true(ok, F("short gesture completes within timeout"));

  int32_t hold = measured_hold_ms();
  Serial.print(F("Measured hold="));
  Serial.print(hold);
  Serial.println(F("ms"));

  const int32_t exp = (int32_t)T_DVR_PRESS_SHORT_MS;
  const int32_t tol = 60;
  expect_true(hold >= (exp - tol) && hold <= (exp + tol), F("short press hold within tolerance"));

  // Immediately attempt another toggle: should be BUSY due to guard window
  dvr_ctrl_req_result_t r3 = dvr_request_toggle_record(&g_ctrl, millis());
  print_req("toggle(immediate after done)", r3);
  expect_true(r3.status == DVR_CTRL_REQ_BUSY, F("guard blocks immediate re-press"));

  // Wait past guard and try again: should accept
  delay((uint32_t)T_DVR_PRESS_GAP_MS + 20u);
  dvr_ctrl_req_result_t r4 = dvr_request_toggle_record(&g_ctrl, millis());
  print_req("toggle(after gap)", r4);
  expect_true(r4.status == DVR_CTRL_REQ_ACCEPTED, F("toggle accepted after guard"));
  (void)run_until_idle((uint32_t)T_DVR_PRESS_SHORT_MS + (uint32_t)T_DVR_PRESS_GAP_MS + 1500u);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { /* Nano usually continues anyway */ }

  Serial.println(F("\n=============================="));
  Serial.println(F("dvr_ctrl SMOKE TEST START"));
  Serial.println(F("=============================="));

  Serial.print(F("Timings: short=")); Serial.print(T_DVR_PRESS_SHORT_MS);
  Serial.print(F(" long=")); Serial.print(T_DVR_PRESS_LONG_MS);
  Serial.print(F(" gap=")); Serial.print(T_DVR_PRESS_GAP_MS);
  Serial.println(F(" (ms)"));

  test_1_init_releases();
  test_2_illegal_toggle_rejected();
  test_3_power_on_long_press_waveform();
  test_4_power_on_noop();
  test_5_toggle_short_press_and_guard();

  Serial.println(F("\n=============================="));
  Serial.print(F("SMOKE TEST COMPLETE. FAILURES="));
  Serial.println(g_fail_count);
  Serial.println(F("=============================="));

  if (g_fail_count == 0) {
    Serial.println(F("OVERALL: PASS"));
  } else {
    Serial.println(F("OVERALL: FAIL"));
  }
}

void loop() {
  // Nothing: tests run once in setup()
}
