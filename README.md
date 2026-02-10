## TL;DR

This repository contains **bare-metal, non-blocking firmware** for an ATmega328-class microcontroller that **wraps a consumer DVR module (RunCam Split-H)** with a deterministic, single-button user interface.

The firmware:

* Observes and classifies the DVR **status LED** in real time
* Emulates DVR **power/shutter button presses** via timed contact closure
* Enforces **safe power-up, recording, and shutdown** sequencing
* Implements a strict **event-driven state machine** with explicit timeouts

The purpose is to transform a **state-opaque, unreliable consumer camera** into a **predictable embedded subsystem** suitable for battery-powered and fault-sensitive applications.

No delays. No RTOS. No guessing what state the camera is in.

---

## What’s in this repository

This repo contains the firmware and supporting headers that implement the Randall Sport Camera controller:

* **Event-driven state machine** for the overall product behaviour (OFF → BOOTING → SELF_TEST → READY_WAITING → RECORDING → STOP/SHUTDOWN, plus error states)
* **DVR control** module that generates deterministic short/long contact-closure gestures (no `delay()`-based blocking)
* **DVR LED classifier** that timestamps LED edges and classifies patterns:

  * **Solid** (idle)
  * **Slow blink** (recording)
  * **Fast blink** (SD/card error)
  * **Abnormal boot signature** (module fault)
* **Battery / fuel-gauge** ADC sampling and thresholding
* **User feedback** (status LED + beep/haptic patterns)
* **Safety shutdown** flow that stops recording then asserts **KILL#** for hard power cut via the power-path controller

> The architecture is deliberately conservative: the DVR is treated as an *untrusted black box*, so the controller only assumes state when it has an LED-derived classification (or a defined timeout).

---

## Hardware assumptions

The firmware expects a board broadly matching this pin intent (ATmega328P):

### Inputs

* **PD2 / INT0**: power-path / wake interrupt (e.g. LTC2954 INT#)
* **PD3**: DVR LED sense input (for pattern capture/classification)
* **PC0 / ADC0**: battery sense (fuel gauge ADC)

### Outputs

* **PD7**: DVR button emulation output driving a PhotoMOS/SSR (contact closure)
* **PD6**: controller status LED
* **PD5**: buzzer / haptic output (PWM-capable)
* **PB1**: **KILL#** to power-path controller (terminal power cut)

If your wiring differs, you must update the pin mapping header(s) before flashing.

---

## How the controller behaves (high level)

The controller presents a simple user experience:

* **Long press**: power on / power off
* **Short press**: start recording / stop recording
* Continuous LED monitoring is used to **confirm** DVR state transitions (idle ↔ recording) and detect fault patterns.
* On **low battery during recording**, the controller performs a silent “dying gasp” stop and then hard power cut.

---

## Getting started

### 1) Prerequisites

* An **ATmega328P** target (development: Arduino Nano; production: ATmega328P-AU TQFP-32 board)
* A **RunCam Split-H** DVR module wired for:

  * power rail control (if applicable)
  * button contact closure (via SSR/PhotoMOS)
  * LED sense line to the MCU
* A way to flash firmware:

  * Arduino bootloader (Nano), **or**
  * ISP programmer for the production board

### 2) Build and flash

Typical workflow (recommended): **VS Code + PlatformIO**

1. Clone this repository.
2. Open the folder in VS Code.
3. Install **PlatformIO**.
4. Select the correct environment/board target.
5. Build.
6. Flash.

If you are using an ISP programmer for the production board, ensure the SPI/ISP pins are not loaded by external circuitry during programming.

### 3) Run (serial optional)

If the build enables debug logging, open a serial monitor at the configured baud rate.
You should see boot banners and state/transition logging during bring-up and smoke tests.

---

## Configuration

The project uses dedicated headers for constants and definitions. The intent is that **behavioural tuning** is done by adjusting constants, not rewriting logic.

* **Pin mapping**: update the `pins` header to match your board.
* **Timing policy**: update `timings` constants (boot gesture duration, classifier windows, guard times).
* **Battery thresholds**: update ADC thresholds (knee, low, critical, lockout hysteresis).
* **Enumerations**: events/states/reasons are centralised to keep the FSM deterministic and testable.

---

## Repo navigation (where to look first)

If you are new here, start in this order:

1. **`enums.h`** — canonical event/state/reason enums used across the codebase
2. **`timings.h`** — all timing constants (gesture timings, classifier windows, timeouts)
3. **`thresholds.h`** (or equivalent) — battery ADC thresholds and hysteresis
4. **FSM / controller module** — the single authoritative state machine
5. **DVR LED classifier** — edge capture → duration measurement → classification events
6. **DVR control module** — schedules contact-closure pulses, enforces guard times

(Exact filenames may vary depending on branch/version; the conceptual modules above are stable.)

---

## Extending / porting

This architecture ports cleanly to other “one-button + LED” DVR modules:

* Keep the **event-driven** structure.
* Replace the LED classifier thresholds/patterns to match the new device.
* Keep the non-blocking gesture scheduler.
* Preserve the safety shutdown semantics.

---

## Safety notes

* **KILL# is terminal**: once asserted, firmware execution stops.
* Do not allow any GPIO to **back-power** the DVR through protection diodes when the DVR is off.
* If you change SSR/PhotoMOS wiring, re-validate that the MCU never sources current into the DVR button line.

---

## License

Add your license here (MIT / BSD / GPL / proprietary).
