# Turret TODO

Current state (2026-11-24): steppers (pan/tilt), VL53L0X ToF (inches), HC-SR501 PIR, SSD1306 OLED
and the Python → USART2 coordinate link are all in. Tracking now runs in **velocity mode** with
Zephyr's `stepper_ctrl` API (like AccelStepper `setSpeed()` + `runSpeed()`). Pan tracks correctly;
the tilt fix is in but **not yet tested on the hardware**.

Architecture today: one `main` thread running a superloop (poll ToF → coords → OLED → PIR → set
motor speeds → sleep). The steppers already step in the background (Zephyr's system workqueue), and
coords come in through a UART ISR → `k_msgq` handoff in `drivers/coords.c`.
Plan: finish tuning the tracking, **check with the professor whether Zephyr is allowed** (section 0),
then do the RTOS work in section 2 (or the CubeMX port in section 4).

Older notes (up to 2026-11-22) are in git history: `git show 91e6897:todo.md`.

---

## Done today (2026-11-24)

- [x] Replaced the old blocking bit-banged `sensors/stepper.c/.h` (deleted) with Zephyr's
      `zephyr,gpio-step-dir-stepper-ctrl` driver (`pan_ctrl` / `tilt_ctrl` nodes in the overlay,
      `CONFIG_STEPPER=y`, `CONFIG_GPIO_STEPPER_COUNTER_TIMING=n`)
- [x] Switched from position mode (move N steps) to **velocity mode** (`stepper_ctrl_run()`): each
      new camera message sets a speed, and the motor keeps running at it until the next message
- [x] Rewrote tracking as left / right / stop (and up / down / stop) with helpers:
      `pan_left()`, `pan_right()`, `pan_stop()`, `tilt_up()`, `tilt_down()`, `tilt_stop()`
- [x] Gains are always **positive** (speed = Kp × |error|). Direction comes only from the
      `PAN_LEFT/RIGHT` and `TILT_UP/DOWN` defines (now swapped: LEFT/UP = POSITIVE)
- [x] Startup direction test in `startup_seq()` (pan L/R, tilt U/D): works for both axes
- [x] Safety stops: `NONE` from Python stops both motors, and so does going 300 ms without a message
- [x] One log line per camera message: `errX .. -> LEFT/RIGHT/STOP .. sps | errY .. -> UP/DOWN/STOP .. sps`
- [x] Pan tracking works
- [x] Found why tilt barely moved while tracking: `Kp_TILT` was negative, so every speed came out
      below `MIN_SPS` and tilt stopped each frame. Now positive (4.0)

---

## 0. Ask the professor (do this first)

- [ ] Ask whether the final project can use **Zephyr on the STM32F407**, or if it has to be
      CubeMX / HAL (or bare-metal). The board is an STM32 either way; the question is the software.
- [ ] Mention what the project shows: threads / message queues / ISRs, and the STM32 pins and
      peripherals set up through devicetree. Offer to explain in the report how Zephyr maps to the hardware.
- [ ] Until there's an answer: keep working on things that carry over either way (tuning, Python,
      `parse_line`, OLED layout). Hold off on the big Zephyr-only work (section 2).
- [ ] Answer: ______

---

## 1. NOW: finish tuning the tracking

Constants are at the top of `src/main.c`.

| Constant | Now | What it does |
|---|---|---|
| `CENTERX` / `CENTERY` | 320 / 240 | frame center (half the camera resolution) |
| `Kp_PAN` / `Kp_TILT` | 15 / 4 | steps/s per pixel of error. **Keep positive** |
| `MAX_PAN` / `MAX_TILT` | 2500 / 700 | speed limits (steps/s) |
| `DEADZONE_PX` | 10 | errors smaller than this → stop |
| `MIN_SPS` | 20 | slower than this counts as stop |
| `COORDS_TIMEOUT_MS` | 300 | no message for this long → stop both motors |

### 1.1 Test tilt with the fix
- [ ] Flash and move your face up and down. Watch `errY .. -> UP/DOWN`
- [ ] If tilt turns **away** from the face, swap `TILT_UP` / `TILT_DOWN`. Never make `Kp_TILT` negative.
- [ ] If tilt overshoots or jerks, lower `Kp_TILT` or `MAX_TILT` (tilt holds the camera against gravity)

### 1.2 Tune the gains
- [ ] Too slow to reach the target → raise `Kp`. Swings back and forth past center → lower it.
- [ ] Buzzes or hums but doesn't turn at high speed → it's stalling (full step, no acceleration).
      Lower `MAX_PAN` / `MAX_TILT`
- [ ] Twitches when centered → raise `DEADZONE_PX`. Stops noticeably off center → lower it.
- [ ] Final values: `Kp_PAN` ____ `Kp_TILT` ____ `MAX_PAN` ____ `MAX_TILT` ____ `DEADZONE_PX` ____

### 1.3 If it's still jerky or stalls: add a software acceleration ramp
- [ ] The Zephyr GPIO stepper controller has **no acceleration**: it jumps straight to the new speed
      (and full stop → full speed when leaving the dead zone)
- [ ] Keep a "current speed" per axis and move it toward the target speed by at most
      `ACCEL * dt` each loop pass. Only call `stepper_ctrl_run()` when the speed actually changes
      (each `run()` call restarts the step timer and toggles STEP once)
- [ ] Note: speeds are quantized by the 10 kHz tick (100 µs resolution), so fast speeds jump in
      big steps: ~5000, 2500, 1667, 1250 sps...

### 1.4 Clean up after tuning
- [ ] Remove the `dir test` block at the end of `startup_seq()` (otherwise the turret moves on every boot)
- [ ] Turn the per-message `errX/errY` log into `LOG_DBG` or log-on-change
- [ ] Only redraw the OLED on a new message (it redraws every 10 ms pass right now, which slows the loop)
- [ ] Commit (the `stepper_ctrl` switch is still uncommitted)

---

## 2. RTOS refactor (after tracking is tuned, and only if Zephyr is allowed)

**Why:** everything still runs in one superloop, so a slow OLED redraw or ToF read delays the next
speed update, every input is polled, and nothing has a priority.

**Already done:** the motors no longer block the loop. `stepper_ctrl` steps them in the background
from the system workqueue, and pan and tilt run at the same time. That was the biggest problem in the old plan.

**Target design:** threads with priorities that **wait on events** instead of polling.

```
   UART ISR ──k_msgq──► [tracking thread]  prio 3
                         waits on queue (K_FOREVER), computes errX/errY,
                         calls pan_left/right/stop + tilt_up/down/stop
                         (+ a timeout: stops motors if no message in 300 ms)

   (steppers: already stepped in the background by stepper_ctrl / system workqueue)

   [ToF thread] prio 4  ── reads every ~50 ms, updates shared state (mutex)
   PIR GPIO ISR ──k_sem/k_event──► wakes tracking ("motion!") instead of polling
   [display thread] prio 7 (lowest) ── redraws 5×/s from shared state
```

(Lower number = higher priority in Zephyr.)

| Concept | Where it shows up |
|---|---|
| Threads + priorities (`K_THREAD_DEFINE`) | Tracking above ToF, display lowest. A slow OLED redraw can't delay a speed update. |
| Blocking on kernel objects | Tracking thread sleeps on `k_msgq_get(..., timeout)` and uses no CPU until a coordinate arrives |
| Interrupt → thread handoff | Already done for UART. Add it for PIR with `gpio_add_callback` + `k_sem` |
| Shared data + `k_mutex` | ToF distance and the current target are read by several threads |
| Workqueues | `stepper_ctrl` already uses the system workqueue to time the step pulses; worth explaining in the report |
| Stack sizing (`CONFIG_THREAD_ANALYZER`) | Each thread has its own stack; measure what each one really uses |
| Priority inversion (more advanced) | Low-priority display holding a mutex others need. `k_mutex` uses priority inheritance to prevent it |

### 2.1 Tracking thread on the coords queue
- [ ] Replace the `K_NO_WAIT` polling in `coords_get()` with a thread that blocks on
      `k_msgq_get(&line_q, ..., K_MSEC(COORDS_TIMEOUT_MS))`. A timeout result means "Python went quiet" → stop the motors
- [ ] It parses the line, computes the error, and sets the motor speeds (the left/right/stop code moves here)
- [ ] Publish the latest target (coords + found) to shared state for the display

### 2.2 ToF and display threads
- [ ] ToF thread: read every ~50 ms, store distance + `near` in shared state under a `k_mutex`
- [ ] Display thread (lowest priority): redraw ~5×/s from shared state
- [ ] Keep the mutex held only long enough to copy the data. Never draw to the OLED while holding it.

### 2.3 PIR on a GPIO interrupt
- [ ] `gpio_pin_interrupt_configure_dt(..., GPIO_INT_EDGE_RISING)` + `gpio_add_callback`
- [ ] ISR gives a `k_sem` (or posts a `k_event`); a thread waits on it and logs / wakes tracking
- [ ] No more `pir_detect()` polling

### 2.4 `main` becomes setup only
- [ ] `main()` runs `startup_seq()` and returns; the threads do the work
- [ ] Threads that need hardware wait until `startup_seq()` has finished (a "ready" `k_sem` / `k_event`)

### 2.5 Measure it
- [ ] `CONFIG_THREAD_ANALYZER=y` (+ `CONFIG_THREAD_NAME=y`) for stack usage and CPU time per thread
- [ ] Right-size each thread's stack from the numbers
- [ ] Optional stretch: replace the mutex-protected shared state with **zbus**

---

## 3. Smaller cleanups (any time)

### 3.1 OLED
- [ ] Optional: `clock-frequency = <I2C_BITRATE_FAST>;` on `&i2c1` for 400 kHz redraws
- [ ] Optional: show `NEAR` / motion, using lines 3–4 (the 10x16 font fits 4 lines on 64 px)

### 3.2 Serial log noise
- [ ] The `distance` log is commented out. Bring it back as "log on change > ~0.5 in" or `LOG_DBG`
- [ ] `target x=.. y=..`: log on change, not every message
- [ ] Python side: drop the `print` in the camera loop once things are stable

### 3.3 Python script hygiene
- [ ] Always send whole numbers: `f"X:{int(x)},Y:{int(y)}\n"` (`X:320.0` is rejected)
- [ ] Clamp to the frame: `max(0, min(x, W - 1))`, since negative / off-frame values are rejected
- [ ] Send `NONE\n` when the target is lost, and **never resend the last x/y** (stale coords make
      the turret keep running in one direction)
- [ ] Write down the camera resolution (e.g. 640x480) and frame center here: ______

### 3.4 Repo / hardware
- [ ] **`build/` is still tracked in git**: `git rm -r --cached build && echo build/ > .gitignore`, then commit
- [ ] Delete the stray empty `sensors/west` file
- [ ] PIR (HC-SR501) tuning: **Tx** pot fully CCW, trigger jumper on **H**, adjust **Sx** for range,
      note the ~2.5 s re-trigger blind time
- [ ] README: fix the sensor name spelling ("VF53L0X" → "VL53L0X")
- [ ] Decide what the PIR and ToF do in tracking mode (e.g. PIR wakes the turret, ToF distance gates firing)
- [ ] Optional: sweep or return home after N seconds with no target
- [ ] Optional: microstepping on the A4988s (MS1–MS3 high = 1/16) for smoother, quieter motion.
      Multiply speeds by 16 if you do.

---

## 4. Backup plan: port to STM32CubeMX / HAL (only if Zephyr isn't allowed)

Your own logic carries over almost unchanged; the drivers and config are what get rewritten.

| Part | CubeMX / HAL version | Difficulty |
|---|---|---|
| PIR (PE7) | GPIO input, `HAL_GPIO_ReadPin` | Easy |
| Logging | `printf` sent out USART2 by overriding `_write()` | Easy |
| Coords on USART2 | `HAL_UART_Receive_IT` byte by byte (or DMA + idle line); `parse_line()` copies over | Easy–moderate |
| SSD1306 | Open-source `stm32-ssd1306` library on `hi2c1`; `oled_*` functions stay almost the same | Easy |
| VL53L0X | ST's VL53L0X API: write its I2C platform functions | Moderate (most tedious) |
| Steppers | PE9 = TIM1_CH1, PE11 = TIM1_CH2: output-compare toggle mode, add each channel's interval to its CCR in the ISR. Write the acceleration ramp too | Moderate |
| RTOS | FreeRTOS (CMSIS-RTOS v2) from CubeMX: `osThreadNew`, `osMessageQueue`, `osMutex` | Moderate |

- [ ] Run CubeIDE on Windows (not WSL); the Discovery's on-board ST-Link works directly
- [ ] Keep your code inside `/* USER CODE BEGIN */ ... END */` blocks, or CubeMX regeneration deletes it
