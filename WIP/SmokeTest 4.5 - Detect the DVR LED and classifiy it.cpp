/*
  DVR LED timing profiler (attachInterrupt) — EDGE-TIMESTAMP + ON/OFF DURATION PRINTS (NO BUZZER)

  Hardware:
  - D3 (INT1)  = READ_DVR (LOW = DVR LED ON)
  - D6         = Test/status LED

  Output:
  - ON_dur_ms   : time LED stayed ON (LOW) since last edge
  - OFF_dur_ms  : time LED stayed OFF (HIGH) since last edge
  - ON->ON ms   : full period estimate (successive ON edges)
  - OFF->OFF ms : full period estimate (successive OFF edges)
  - PATTERN ->  : OFF / SOLID / SLOW_BLINK / FAST_BLINK / UNKNOWN

  Notes:
  - Constants here are harness placeholders. Once you confirm measured ranges,
    copy the final numbers into timings.h / thresholds.h.
*/

#include <Arduino.h>

static constexpr uint8_t PIN_READ_DVR = 3;  // D3 / INT1
static constexpr uint8_t PIN_LED      = 6;  // Status LED

// Harness thresholds (you can align to timings.h after measuring)
static constexpr uint16_t T_SOLID_MS     = 1500;
static constexpr uint16_t T_SLOW_MIN_MS  = 1500;
static constexpr uint16_t T_SLOW_MAX_MS  = 3000;
static constexpr uint16_t T_FAST_MIN_MS  = 80;
static constexpr uint16_t T_FAST_MAX_MS  = 450;

static constexpr uint16_t GLITCH_US      = 3000; // 3ms glitch reject

enum class DvrPat : uint8_t { UNKNOWN, OFF, SOLID, SLOW_BLINK, FAST_BLINK };

// --- ISR edge ring buffer ---
static constexpr uint8_t QN = 32;               // power-of-two
static volatile uint32_t q_ts[QN];              // micros() timestamps
static volatile uint8_t  q_lvl[QN];             // pin level AFTER edge
static volatile uint8_t  q_w = 0, q_r = 0;

static volatile uint32_t s_last_isr_us = 0;

static void isr_dvr_change()
{
  const uint32_t now_us = micros();
  const uint32_t dt_us  = now_us - s_last_isr_us;
  if (dt_us < GLITCH_US) return;
  s_last_isr_us = now_us;

  const uint8_t w = q_w;
  const uint8_t w_next = (uint8_t)((w + 1) & (QN - 1));
  if (w_next == q_r) return;                    // overflow => drop

  q_ts[w]  = now_us;
  q_lvl[w] = (uint8_t)digitalRead(PIN_READ_DVR);
  q_w = w_next;
}

static bool pop_edge(uint32_t &ts_us, uint8_t &lvl)
{
  noInterrupts();
  if (q_r == q_w) { interrupts(); return false; }
  const uint8_t r = q_r;
  ts_us = q_ts[r];
  lvl   = q_lvl[r];
  q_r = (uint8_t)((r + 1) & (QN - 1));
  interrupts();
  return true;
}

// --- Classifier + measurement state ---
static DvrPat   s_pat = DvrPat::UNKNOWN;
static uint32_t s_last_edge_ms = 0;

// For ON->ON / OFF->OFF full-period estimates
static uint32_t s_last_on_us  = 0;  // last transition into ON (LOW)
static uint32_t s_last_off_us = 0;  // last transition into OFF (HIGH)

// For ON/OFF duration (adjacent edge delta)
static uint32_t s_prev_edge_us = 0;
static uint8_t  s_prev_level   = HIGH; // level that was held BEFORE current edge

static inline uint16_t u16_sat(uint32_t v) { return (v > 0xFFFFu) ? 0xFFFFu : (uint16_t)v; }

static void print_pat(DvrPat p)
{
  static DvrPat last = DvrPat::UNKNOWN;
  if (p == last) return;
  last = p;

  Serial.print(F("PATTERN -> "));
  switch (p)
  {
    case DvrPat::OFF:        Serial.println(F("OFF")); break;
    case DvrPat::SOLID:      Serial.println(F("SOLID")); break;
    case DvrPat::SLOW_BLINK: Serial.println(F("SLOW_BLINK")); break;
    case DvrPat::FAST_BLINK: Serial.println(F("FAST_BLINK")); break;
    default:                 Serial.println(F("UNKNOWN")); break;
  }
}

static inline DvrPat classify_period(uint16_t per_ms)
{
  if (per_ms >= T_FAST_MIN_MS && per_ms <= T_FAST_MAX_MS) return DvrPat::FAST_BLINK;
  if (per_ms >= T_SLOW_MIN_MS && per_ms <= T_SLOW_MAX_MS) return DvrPat::SLOW_BLINK;
  return DvrPat::UNKNOWN;
}

void setup()
{
  pinMode(PIN_READ_DVR, INPUT);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  Serial.begin(115200);
  delay(200);
  Serial.println(F("DVR LED timing profiler (EDGE-TIMESTAMP, NO BUZZER)"));

  attachInterrupt(digitalPinToInterrupt(PIN_READ_DVR), isr_dvr_change, CHANGE);

  const uint32_t now_ms = millis();
  s_last_edge_ms = now_ms;

  // Seed previous level for duration accounting
  s_prev_level = (uint8_t)digitalRead(PIN_READ_DVR);
  s_prev_edge_us = micros();

  s_pat = DvrPat::UNKNOWN;
  print_pat(s_pat);
}

void loop()
{
  const uint32_t now_ms = millis();

  // Mirror instantaneous phase
  const bool dvrLedOn = (digitalRead(PIN_READ_DVR) == LOW);
  digitalWrite(PIN_LED, dvrLedOn ? HIGH : LOW);

  // Drain queued edges
  uint32_t ts_us;
  uint8_t lvl_after;

  while (pop_edge(ts_us, lvl_after))
  {
    s_last_edge_ms = now_ms;

    // Duration of the PREVIOUS held level (adjacent edge delta)
    const uint32_t held_us = ts_us - s_prev_edge_us;
    const uint16_t held_ms = u16_sat(held_us / 1000u);

    // s_prev_level was held until this edge
    if (s_prev_level == LOW)
    {
      Serial.print(F("ON_dur_ms="));
      Serial.println(held_ms);
    }
    else
    {
      Serial.print(F("OFF_dur_ms="));
      Serial.println(held_ms);
    }

    // Update prev-edge tracking for next duration measurement
    s_prev_edge_us = ts_us;
    s_prev_level   = lvl_after; // after-edge level is the new held level

    // Full-period estimates
    const bool ledOnNow = (lvl_after == LOW);

    if (ledOnNow)
    {
      if (s_last_on_us != 0)
      {
        const uint16_t on_to_on_ms = u16_sat((ts_us - s_last_on_us) / 1000u);
        Serial.print(F("ON->ON ms="));
        Serial.println(on_to_on_ms);

        const DvrPat bp = classify_period(on_to_on_ms);
        if (bp != DvrPat::UNKNOWN) { s_pat = bp; print_pat(s_pat); }
      }
      s_last_on_us = ts_us;
    }
    else
    {
      if (s_last_off_us != 0)
      {
        const uint16_t off_to_off_ms = u16_sat((ts_us - s_last_off_us) / 1000u);
        Serial.print(F("OFF->OFF ms="));
        Serial.println(off_to_off_ms);

        const DvrPat bp = classify_period(off_to_off_ms);
        if (bp != DvrPat::UNKNOWN) { s_pat = bp; print_pat(s_pat); }
      }
      s_last_off_us = ts_us;
    }
  }

  // Quiet-time classification
  const uint32_t quiet_ms = now_ms - s_last_edge_ms;
  const bool in_blink = (s_pat == DvrPat::SLOW_BLINK || s_pat == DvrPat::FAST_BLINK);

  if (!in_blink && quiet_ms >= T_SOLID_MS)
  {
    s_pat = dvrLedOn ? DvrPat::SOLID : DvrPat::OFF;
    print_pat(s_pat);
  }

  if (in_blink && quiet_ms >= (uint32_t)(T_SOLID_MS + T_SLOW_MAX_MS))
  {
    s_pat = dvrLedOn ? DvrPat::SOLID : DvrPat::OFF;
    print_pat(s_pat);
  }
}
