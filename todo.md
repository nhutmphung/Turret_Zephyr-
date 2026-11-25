# Turret TODO

Current state (2026-11-24): steppers (pan/tilt), VL53L0X ToF (inches), HC-SR501 PIR, SSD1306 OLED
and the Python → USART2 coordinate link are all in. The OLED shows the target coords + distance
(`oled_show_coords`). `main.c` has a first **proportional tracking** loop (dead zone, step clamp,
only moves on a new target), committed in `91e6897`, **not yet bench-tested with the motors**.

Architecture today: one `main` thread running a superloop (poll ToF → coords → OLED → PIR → move
steppers → sleep). The only RTOS mechanism in use is the UART ISR → `k_msgq` handoff in `drivers/coords.c`.
Plan: get the steppers working as intended first, **then** move to the RTOS design in section 2.

Older notes (up to 2026-11-22: ToF bring-up, OLED setup, coords link setup) were cleared out.
They're in git history if needed: `git show 91e6897:todo.md`.

---

## 1. NOW (2026-11-24): finish the steppers + bench-test the tracking values

The tracking code is in `src/main.c` (top-of-file constants + the `if (new_coords && tgt.found)` block).
Goal: the turret turns **toward** the target, quickly, without overshooting.

Starting values in `main.c`:

| Constant | Now | What it does |
|---|---|---|
| `CENTERX` / `CENTERY` | 320 / 240 | frame center (half the camera resolution) |
| `Kp_PAN` / `Kp_TILT` | 1.0 / 1.0 | steps per pixel of error (negative flips direction) |
| `DEADZONE_PX` | 10 | errors smaller than this are ignored |
| `MAX_STEPS` | 200 | most steps in one correction (~0.2 s per axis at 1 ms/step) |

### 1.0 Fix first
- [ ] `stepper_move(&pan, stepsX*100)` / `stepsY*100`: the `*100` happens **after** the clamp, so one
      correction can be up to ±20,000 steps (~20 s with the loop blocked). Drop the `*100` and raise
      `Kp` or `MAX_STEPS` instead, so the clamp still limits the move.
- [ ] Log labels are swapped: the pan move logs `steperY`, the tilt move logs `steperX`

### 1.1 Before powering the motors
- [ ] Confirm the camera resolution in the Python script and set `CENTERX` / `CENTERY` to half of it
- [ ] Flash with the **motor driver unpowered**, then check the serial log and the OLED: `target x=.. y=..`, distance, `no target` on `NONE`
- [ ] Temporarily log the math so you can sanity-check it without anything moving:
      `LOG_INF("err %d,%d steps %d,%d", errX, errY, stepsX, stepsY);`
- [ ] Make sure the turret can't hit anything or wrap its cables if an axis runs the wrong way.
      Keep a hand on the driver power.

### 1.2 Directions (one axis at a time)
- [ ] Power the motors. Put the target slightly **left** of center: pan should turn toward it
- [ ] If pan turns away, make `Kp_PAN` negative (`-1.0f`) and re-test
- [ ] Same for tilt: target slightly **above** center, flip `Kp_TILT` if it goes the wrong way
- [ ] Note the final signs here: pan ____ tilt ____

### 1.3 Gains (`Kp_PAN`, `Kp_TILT`)
- [ ] Start at 1.0. If it barely creeps toward the target, try 2, 4, 8...
- [ ] If it overshoots and swings back and forth, halve it
- [ ] Goal: settles on the target in a few corrections with no back-and-forth
- [ ] Pan and tilt will probably need different values (different load / gearing)
- [ ] Useful number to measure: **steps per pixel**. Move one axis a known number of steps
      (e.g. 100) and see how many pixels the target shifts. `Kp ≈ 1 / (pixels per step)` is the
      "one jump to center" gain; use somewhat less than that to avoid overshoot.
- [ ] Final values: `Kp_PAN` ____ `Kp_TILT` ____

### 1.4 Dead zone and step clamp
- [ ] `DEADZONE_PX`: raise it if the turret twitches when the target is still and centered;
      lower it if it stops noticeably off center
- [ ] `MAX_STEPS`: raise it if big moves take too many corrections; lower it if a bad frame causes a big swing
- [ ] Final values: `DEADZONE_PX` ____ `MAX_STEPS` ____

### 1.5 Things to watch for while testing
- [ ] Lag: the camera frame that arrives after a move may have been taken **during** the move, which
      can cause overshoot even with a good `Kp`. If so, lower `Kp` or add a short pause after a move.
- [ ] Missed steps / stalls at high speed. `STEP_PERIOD_US` in `stepper.c` is 1000 µs; don't lower it until gains are sorted.
- [ ] Python sending floats or off-frame values → `bad coords line` warnings (see 3.3)
- [ ] Remove the temporary `err/steps` log once the values are set
- [ ] Commit the tuned values

---

## 2. RTOS refactor (after the steppers work as intended)

**Why:** today everything runs in one superloop, so the slowest task holds up all the others.
`stepper_move()` blocks the whole loop (coords go stale, OLED freezes, ToF and PIR are missed),
pan and tilt can't move at the same time, step timing depends on how long the OLED and ToF took,
every input is polled, and nothing has a priority. These are exactly the problems an RTOS solves.

**Target design:** threads with priorities that **wait on events** instead of polling.

```
                 UART ISR ──k_msgq──► [tracking thread]  prio 3
                                       waits on queue (K_FOREVER),
                                       computes target pan/tilt position
                                            │ writes target (mutex)
                                            ▼
                           [pan stepper thread]  [tilt stepper thread]  prio 1–2 (highest)
                            step toward target, one step per tick; both move at once

   [ToF thread] prio 4  ── reads every ~50 ms, updates shared state (mutex)
   PIR GPIO ISR ──k_sem/k_event──► wakes tracking ("motion!") instead of polling
   [display thread] prio 7 (lowest) ── redraws 5×/s from shared state
```

(Lower number = higher priority in Zephyr.)

**RTOS concepts this covers:**

| Concept | Where it shows up |
|---|---|
| Threads + priorities (`K_THREAD_DEFINE`) | Steppers highest, display lowest. A slow OLED redraw can never delay a step. |
| Blocking on kernel objects | Tracking thread sleeps on `k_msgq_get(..., K_FOREVER)` and uses 0% CPU until a coordinate arrives |
| Interrupt → thread handoff | Already done for UART. Add it for PIR with `gpio_add_callback` + `k_sem` |
| Shared data + `k_mutex` | ToF distance and the current target are read by several threads |
| Precise periodic timing (`k_timer`, or sleeping until a fixed time) | Steady step rate and sensor sample rate that don't drift with the loop |
| Stack sizing (`CONFIG_THREAD_ANALYZER`) | Each thread has its own stack; measure what each one really uses |
| Priority inversion (more advanced) | Low-priority display holding a mutex the steppers need. Zephyr's `k_mutex` uses priority inheritance to prevent it |

### 2.1 Stepper thread(s) (biggest win, do first)
- [ ] Give `stepper.c` a **target position** per axis (`stepper_set_target(axis, pos)`) instead of a blocking `stepper_move(steps)`
- [ ] One high-priority thread per axis (or one thread for both) that steps toward its target, one step per period
- [ ] Protect the target / current position with a `k_mutex` (or `atomic_t`)
- [ ] `main` / tracking sets new targets and returns immediately; pan and tilt now move **at the same time**
- [ ] Optional: drive the step pulses from a `k_timer` for a steadier step rate

### 2.2 Tracking thread on the coords queue
- [ ] Replace the `K_NO_WAIT` polling in `coords_get()` with a thread that blocks on `k_msgq_get(&line_q, ..., K_FOREVER)`
- [ ] It parses the line, computes the error from center, and calls `stepper_set_target(...)`
- [ ] Publish the latest target (coords + found) to shared state for the display

### 2.3 ToF and display threads
- [ ] ToF thread: read every ~50 ms, store distance + `near` in shared state under a `k_mutex`
- [ ] Display thread (lowest priority): redraw ~5×/s from shared state (also fixes the "redraws every pass" OLED issue)
- [ ] Keep the mutex held only long enough to copy the data. Never draw to the OLED while holding it.

### 2.4 PIR on a GPIO interrupt
- [ ] Configure the PIR pin with `gpio_pin_interrupt_configure_dt(..., GPIO_INT_EDGE_RISING)` + `gpio_add_callback`
- [ ] ISR gives a `k_sem` (or posts a `k_event`); a thread waits on it and logs / wakes tracking
- [ ] No more `pir_detect()` polling in the loop

### 2.5 `main` becomes setup only
- [ ] `main()` runs `startup_seq()` and returns; the threads do all the work
- [ ] Startup ordering: threads that need hardware should wait until `startup_seq()` has finished
      (e.g. start them with a delay, or have them wait on a "ready" `k_sem` / `k_event`)

### 2.6 Measure it
- [ ] `CONFIG_THREAD_ANALYZER=y` (+ `CONFIG_THREAD_NAME=y`) to print stack usage and CPU time per thread
- [ ] Right-size each thread's stack from the numbers
- [ ] Optional stretch: replace the mutex-protected shared state with **zbus** (Zephyr's publish/subscribe bus)

---

## 3. Smaller cleanups (any time)

### 3.1 OLED
- [ ] Only redraw when something changed, or at most every ~100–200 ms (solved by the display thread in 2.3)
- [ ] Optional: `clock-frequency = <I2C_BITRATE_FAST>;` on `&i2c1` for 400 kHz if redraws slow the loop
- [ ] Optional: show `NEAR` / motion, using lines 3–4 (the 10x16 font fits 4 lines on 64 px)

### 3.2 Serial log noise
- [ ] The `distance` log is commented out for now. Bring it back as "log on change > ~0.5 in" or `LOG_DBG`
- [ ] Same idea for `target x=.. y=..`: log on change, not every message
- [ ] Python side: drop the `print` in the camera loop once things are stable (two threads printing jumbles the lines)

### 3.3 Python script hygiene
- [ ] Always send whole numbers: `f"X:{int(x)},Y:{int(y)}\n"` (`X:320.0` is rejected)
- [ ] Clamp to the frame: `max(0, min(x, W - 1))`, since negative / off-frame values are rejected
- [ ] Send `NONE\n` when the target is lost so the board stops tracking
- [ ] Write down the camera resolution (e.g. 640x480) and frame center here: ______

### 3.4 Carried over from older notes
- [ ] **`build/` is still tracked in git**, so build outputs show up as changes in every commit:
      `git rm -r --cached build && echo build/ > .gitignore`, then commit
- [ ] PIR (HC-SR501) tuning on the bench: set the **Tx** pot fully CCW (shortest hold), trigger jumper on
      **H** (repeat trigger), adjust the **Sx** pot for range, and note the ~2.5 s re-trigger blind time
- [ ] README: fix the sensor name spelling ("VF53L0X" → "VL53L0X")
- [ ] Decide what the PIR and ToF do in tracking mode (e.g. PIR wakes the turret, ToF distance gates firing)
- [ ] Optional: sweep or return home after N seconds with no target
