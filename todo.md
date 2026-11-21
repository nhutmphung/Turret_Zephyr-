# Turret TODO

Current state (2026-11-21): steppers (pan/tilt), VL53L0X ToF (inches), HC-SR501 PIR, SSD1306 OLED
and the Python → USART2 coordinate link are all in and building. `main.c` runs `startup_seq()`,
then a loop that reads coords + ToF + PIR (stepper test sweep is commented out).

The older ToF bring-up notes further down (sections 1–8) are kept for reference; most of them are done.

---

## NEXT UP (2026-11-21)

### 1. Show the target on the OLED
- [ ] Add `oled_show_target(const struct coords *c, float inches)` (or `int x, int y, float inches`) to `oled.c` / `oled.h`
      - line 1: `X:320 Y:231`, or `no target` when `found == false`
      - line 2: the ToF distance, e.g. `2.8 in NEAR`
- [ ] In `main.c`, keep the last `struct coords` around (file-scope or before the loop) so the screen
      still has something to show on loop passes where `coords_get()` returns false
- [ ] Only redraw the OLED when something changed, or at most every ~100–200 ms. Each
      `cfb_framebuffer_finalize` is a 1 KB I2C transfer (~25 ms at 100 kHz)
- [ ] Optional: `clock-frequency = <I2C_BITRATE_FAST>;` on `&i2c1` for 400 kHz if redraws slow the loop

### 2. Cut down the serial log noise
- [ ] The `distance ... in` line prints every pass (~every 50 ms) and buries the coords lines
- [ ] Options: log only when the distance changes by more than ~0.5 in, log every Nth pass, or
      make it `LOG_DBG` (hidden at `LOG_LEVEL_INF`)
- [ ] Same idea for `target x=.. y=..`: log on change, not every message
- [ ] Python side: drop the `print` in the camera loop once things are stable. The board's
      `target x=.. y=..` log already confirms what arrived, and two threads printing at once
      jumbles the lines.

### 3. Python script hygiene
- [ ] Always send whole numbers: `f"X:{int(x)},Y:{int(y)}\n"` (`X:320.0` is rejected)
- [ ] Clamp to the frame: `max(0, min(x, W - 1))`, since negative / off-frame values are rejected
- [ ] Send `NONE\n` when the target is lost so the board knows to stop tracking
- [ ] Write down the camera resolution (e.g. 640x480) and frame center here: ______

### 4. Commit the coords work
- [ ] Delete the misspelled draft `drivers/cords.c` (untracked, not compiled, fully replaced by `drivers/coords.c`)
- [ ] Commit `drivers/coords.c`, `drivers/coords.h`, `boards/stm32f4_disco.overlay`, `CMakeLists.txt`,
      `prj.conf`, `src/main.c`, `todo.md`
- [ ] Consider doing the `build/` cleanup from "Repo housekeeping" first, so build outputs stop showing up in every commit

### 5. Actual tracking: pixel offset → pan/tilt steps
- [ ] Error from frame center: `err_x = x - CENTER_X`, `err_y = y - CENTER_Y` (e.g. center 320, 240 for 640x480)
- [ ] Dead zone: ignore small errors (e.g. ±10 px) so the turret doesn't jitter around the target
- [ ] Start with a simple proportional move: `steps = err * K`, clamp to a max step per update, tune `K` on the bench
- [ ] Check directions: if the turret moves away from the target, flip the sign (or the DIR pin polarity) for that axis
- [ ] Don't move on `NONE`. Optionally sweep or return home after N seconds with no target.
- [ ] **The steppers must stop blocking the loop.** `stepper_move()` blocks until the move is done,
      so coords and the ToF go stale while it runs. Options:
      - move a few steps per loop pass toward the target (simplest)
      - a stepper thread (`K_THREAD_DEFINE`) that chases a target position the main loop updates
      - timer/PWM-driven step pulses (most work, smoothest)
- [ ] Decide what the PIR and ToF do in tracking mode (e.g. PIR wakes the turret, ToF distance gates firing)

---

## DONE (2026-11-21): camera coordinates from Python → turret over USART2

Working end to end: camera → Python → USART2 (PA3) → `drivers/coords.c` ISR → parser → `LOG_INF`.
Lesson learned: the Python script was **printing** `X:..,Y:..` but **sending** `309,227` (no labels),
so every line hit `bad coords line`. Build the message once and use the same string for `print` and `ser.write`.
Open items left in this section have moved to NEXT UP above, or are optional upgrades.

Goal: the Python script on the PC finds the target with the camera, sends its (x, y) pixel
coordinates to the STM32 over serial, and the firmware parses them, logs them with `LOG_INF`,
and shows them on the SSD1306.

### 1. Serial link: share USART2 with the logs (no new hardware)
The Zephyr console (logs) is already on **USART2: PA2 = TX, PA3 = RX** (`zephyr,console`), going
to the one USB-TTL adapter. A UART has a separate wire for each direction, so both jobs fit on it:

```
STM32 PA2 (TX) ──────► adapter RX   logs:   STM32 → PC   (already working)
STM32 PA3 (RX) ◄────── adapter TX   coords: PC → STM32   (new)
STM32 GND      ─────── adapter GND
```

- [x] Check the **adapter TX → PA3** wire is actually connected. If you only ever read logs, you
      may have wired just RX + GND. Make sure the adapter's logic level is set to **3.3 V**, not 5 V.
- [x] **Only one PC program can open the port at a time.** The Python script takes over from
      minicom/picocom: it sends coordinates **and** prints the log lines it receives (see step 3).
      Close minicom before running it.
- [x] Keep `CONFIG_SHELL` **off**. The shell would also read incoming bytes on USART2 and swallow the
      coordinates.
- [x] **WSL note:** the adapter must be attached to WSL with `usbipd-win`
      (`usbipd attach --wsl --busid <id>`), or run the Python script on the Windows side with `COMx`.
      The camera may also need to be attached the same way, or it's easier to run the whole
      script on Windows.

Upgrade paths for later, if sharing gets annoying (the firmware code stays the same, only the
overlay/chosen entry changes):
- [ ] **Second USB-TTL adapter (~$5)** on a separate UART, e.g. USART3 on PD8 (TX) / PD9 (RX)
      (check against the Discovery user manual's pin table; avoid PE7, PE9–PE12, PB6, PB9). Logs and
      coordinates on separate ports, and minicom can stay open.
- [ ] **USB CDC-ACM** on the board's micro-USB OTG port (PA11/PA12, `zephyr_udc0` in the board
      DTS). No adapter, shows up as `/dev/ttyACM0`, but needs more USB config in Zephyr.

### 2. Define the message format (keep it dumb and line-based)
- [x] One ASCII line per frame: `X:<int>,Y:<int>\n`, e.g. `X:320,Y:240\n` (pixel coords of the target center)
- [x] `NONE\n` when no target is in frame
- [x] 115200 baud, 8N1 (same settings the console already uses)
- [x] Cap the send rate at ~20–30 Hz in Python. The firmware only needs the latest value.
- [ ] Write down the camera resolution (e.g. 640x480) so the firmware knows where the center is later

### 3. Python side (sender + log viewer in one script)
- [x] `pip install pyserial`
- [x] Open the port once: `ser = serial.Serial("/dev/ttyUSB0", 115200, timeout=0.1)` (or `COMx` on Windows)
- [x] Start a background thread that prints the STM32 logs, since minicom is no longer reading them:
  ```python
  import threading

  def print_logs():
      while True:
          line = ser.readline()
          if line:
              print("[stm32]", line.decode(errors="replace").rstrip())

  threading.Thread(target=print_logs, daemon=True).start()
  ```
- [x] Each frame in the camera loop: `ser.write(f"X:{x},Y:{y}\n".encode())`, or `b"NONE\n"` when nothing is found
- [x] Throttle with a timestamp check so it doesn't flood the UART

### 4. Firmware side
- [x] Overlay: USART2 is already enabled by the board, so **no new UART node**. Add a chosen entry so
      the code doesn't hard-code which UART it uses (moving to USART3 later is a one-line change):
  ```dts
  / {
  	chosen {
  		turret,coords-uart = &usart2;
  	};
  };
  ```
  In code: `DEVICE_DT_GET(DT_CHOSEN(turret_coords_uart))`
- [x] `prj.conf`: `CONFIG_SERIAL=y` (already implied), `CONFIG_UART_INTERRUPT_DRIVEN=y`
- [x] New `drivers/coords.c` / `coords.h` (ended up in `drivers/`, added to the include dirs too) (add it to `CMakeLists.txt`):
      - `coords_init()`: `device_is_ready`, set the RX IRQ callback, `uart_irq_rx_enable`. Only
        enable **RX** interrupts. Logging handles TX on the same UART, so don't touch the TX IRQ.
      - ISR: read bytes into a line buffer, and on `\n` hand the finished line to a `k_msgq`
        (don't parse or log inside the ISR). Drop the line if it overflows the buffer.
      - `bool coords_get(int *x, int *y)`: non-blocking; parse the newest line (`strtol`),
        reject garbage, return false if nothing new
      - Only the **latest** coordinate matters. Use a 1-deep queue and `k_msgq_purge` before
        putting, so old frames don't pile up while the steppers block the main loop.
- [ ] → moved to NEXT UP #1. `oled.c`: add something like `oled_show_target(int x, int y, float inches)` so the screen shows
      `X:320 Y:240` on line 1 and the ToF distance on line 2 (or "no target")
- [x] `main.c`: `coords_init()` with the other inits. In the loop:
      `if (coords_get(&x, &y)) { LOG_INF("target x=%d y=%d", x, y); oled_show_target(...); }`
      The `LOG_INF` goes back out on PA2 and shows up in the Python script's `[stm32]` output, which is
      a handy confirmation that the board really received the coordinate.
- [x] Heads-up: the current test-sweep loop blocks for seconds per iteration, so coordinates
      will look laggy. Fine for this step, but tracking will eventually need the steppers in their
      own thread (or non-blocking moves).

### 5. Test in stages
- [x] Firmware first, no camera. Close minicom, then send a line by hand:
      `python3 -c "import serial,time; s=serial.Serial('/dev/ttyUSB0',115200); s.write(b'X:100,Y:50\n'); time.sleep(0.5); print(s.read_all().decode(errors='replace'))"`
      You should see `target x=100 y=50` in the printed output, and the same values on the OLED.
      (Or use `picocom /dev/ttyUSB0 -b 115200` and type `X:100,Y:50` + Enter. It shows logs and sends what you type.
      You may need `--omap crlf` so Enter sends `\n`.)
- [x] Send bad input (`X:abc`, very long lines, no newline) and make sure nothing crashes
- [x] Then hook up the real camera script and watch the values update live
- [ ] → moved to NEXT UP #5. Later (separate task): turn pixel error from the frame center into pan/tilt steps

---

## Tomorrow (2026-11-19)

### A. Check the HC-SR501 PIR sensitivity
The PIR code is in (`sensors/pir.c`, `pir-gpios` on PE7), so this is tuning and testing on the bench.

- [ ] Find the two pots on the back of the board (orientation varies by clone, so check the silkscreen):
      - **Sx (sensitivity / range)**: roughly 3 m (CCW) to 7 m (CW)
      - **Tx (time delay)**: how long OUT stays high after a trigger, roughly 3 s (CCW) to 5 min (CW)
- [ ] Set **Tx fully CCW** (shortest hold) so the turret can react to new motion quickly
- [ ] Check the trigger jumper: **H = repeat trigger** (stays high while motion continues),
      **L = single trigger**. H is probably what you want for tracking.
- [ ] Wait 30–60 s after power-up for it to settle before testing
- [ ] Walk past at a few distances (1 m, 2 m, 3 m, 5 m) and log what triggers in the serial output
- [ ] Turn Sx down if it false-triggers (heat sources, fans, sunlight), and up if it misses you at the range you need
- [ ] Note the re-trigger blind time (~2.5 s after OUT goes low, when it ignores motion) and decide whether the code needs to handle it
- [ ] Write down the final pot positions / range so it's easy to reproduce

### Done
- [x] SSD1306 128x64 OLED on I2C1 @ 0x3C (shares the bus with the ToF @ 0x29):
      overlay node + `zephyr,display`, `CONFIG_DISPLAY` / `CONFIG_CHARACTER_FRAMEBUFFER` /
      `CONFIG_HEAP_MEM_POOL_SIZE=2048`, `sensors/oled.c` / `oled.h`, shows ToF distance from
      `main.c`. Builds clean, **not yet tested on hardware** (run `west flash`)
- [x] PIR GPIO on PE7 + `sensors/pir.c` / `pir.h` wired into `main.c` (commit `a5d2713`)

### Still open from 2026-11-18: wire the ToF sensor to I2C1
The overlay node (`tof: vl53l0x@29` on `&i2c1`) is already in place, so this is just hardware.

- [ ] VL53L0X **VIN → 3V**, **GND → GND** (check whether your breakout has its own regulator)
- [ ] **SCL → PB6**, **SDA → PB9**
- [ ] Check for pull-ups on SCL/SDA (most breakouts have them; if not, ~4.7 kΩ to 3.3 V)
- [ ] XSHUT: tie it high, or wire it to a free GPIO (not PE9–PE12)
- [ ] Flash and check the serial log for `not ready` / `StaticInit failed`

---

## 1. Devicetree overlay (`boards/stm32f4_disco.overlay`) — **blocks the build**

- [ ] Add the sensor node under `&i2c1`. Without it, `CONFIG_VL53L0X` can't turn on and
      `DEVICE_DT_GET_ONE(st_vl53l0x)` fails to link (`__device_dts_ord_...` undefined).

  ```dts
  &i2c1 {
  	tof: vl53l0x@29 {
  		compatible = "st,vl53l0x";
  		reg = <0x29>;
  		/* optional, but recommended — see section 6 */
  		/* xshut-gpios = <&gpioe 13 GPIO_ACTIVE_LOW>; */
  	};
  };
  ```

  `i2c1` is already `status = "okay"` on the stm32f4_disco at 400 kHz,
  pins **PB6 = SCL, PB9 = SDA**. Wire the sensor there. The board's audio codec (CS43L22 @ 0x4a)
  shares that bus — no address clash with 0x29, so that's fine.

## 2. `prj.conf` — **blocks the build**

- [ ] `CONFIG_VL53L0X_PROXIMITY_THRESHOLD=100` depends on `CONFIG_VL53L0X`, which only
      exists once the DT node from step 1 is there. Right now it gives a Kconfig
      "unsatisfied dependencies" warning/error. Fix step 1 and this resolves itself
      (100 is also the default, so you can just delete the line).
- [ ] Missing newline at end of file (`CONFIG_CBPRINTF_FP_SUPPORT=y` is the last line with no `\n`).
      Harmless today, but appending a line later with `echo >>` will glue it onto this one.
- [ ] Optional: `CONFIG_SENSOR_LOG_LEVEL_DBG=y` while bringing it up — the driver logs why init fails.

## 3. `sensors/vlf53l0x.c` — **won't compile as written**

- [ ] **Rename the file**: the part is the VL53L0X, not "vlf53l0x" (README also says "VF53L0X").
      Suggest `sensors/vl53l0x.c` + `sensors/vl53l0x.h`.
- [ ] **Remove `main()`**. `src/main.c` already has one → `multiple definition of 'main'`.
      Turn it into `vl53l0x_init()` and `vl53l0x_read_mm()` (your own notes, step 3).
- [ ] **Brace / stray code bug**: the function closes `}` *before* `return 0;`, so `return 0;`
      sits at file scope → syntax error. Also `printk("\n"); k_sleep(...)` is *after* the
      `while(1)` loop, so it's unreachable — the sleep belongs inside the loop.
- [ ] **Typo**: `SENOSR_CHAN_DISTANCE` → `SENSOR_CHAN_DISTANCE`.
- [ ] **Bad format string**: `"%.3;;d"` is invalid. `sensor_value_to_milli()` returns `int64_t`,
      so use `printk("distance %d mm\n", (int)sensor_value_to_milli(&value));`
      (or `%lld` with a cast to `long long`).
- [ ] Check the return value of each `sensor_channel_get()` (currently `ret` is set but ignored).
- [ ] `"device/sensor not ready."` is missing `\n`.
- [ ] Don't `return` on a single failed fetch in the read path — log it and try again next loop.
- [ ] Units note: `SENSOR_CHAN_DISTANCE` is in **meters** (`val1` = m, `val2` = µm), so
      `value.val1` alone will be 0 for anything under 1 m. Use `sensor_value_to_milli()`.
      `SENSOR_CHAN_PROX` is 1/0 (closer/farther than the threshold).
- [ ] `#include <zephyr/drivers/sensor/vl53l0x.h>` is only needed for the extra private channels
      (range status, signal rate, DMAX). Keep it if you want `SENSOR_CHAN_VL53L0X_RANGE_STATUS`
      to reject bad readings (status != 0 = invalid range), otherwise drop it.

  Sketch:

  ```c
  /* sensors/vl53l0x.h */
  #pragma once
  int vl53l0x_init(void);
  int vl53l0x_read_mm(int *mm, bool *near);
  ```

  ```c
  /* sensors/vl53l0x.c */
  #include <zephyr/device.h>
  #include <zephyr/drivers/sensor.h>
  #include <zephyr/logging/log.h>
  #include "vl53l0x.h"

  LOG_MODULE_REGISTER(tof, LOG_LEVEL_INF);

  static const struct device *const tof = DEVICE_DT_GET(DT_NODELABEL(tof));

  int vl53l0x_init(void)
  {
  	if (!device_is_ready(tof)) {
  		LOG_ERR("VL53L0X not ready");
  		return -ENODEV;
  	}
  	return 0;
  }

  int vl53l0x_read_mm(int *mm, bool *near)
  {
  	struct sensor_value v;
  	int ret = sensor_sample_fetch(tof);

  	if (ret) {
  		return ret;
  	}
  	ret = sensor_channel_get(tof, SENSOR_CHAN_DISTANCE, &v);
  	if (ret) {
  		return ret;
  	}
  	*mm = (int)sensor_value_to_milli(&v);

  	ret = sensor_channel_get(tof, SENSOR_CHAN_PROX, &v);
  	if (ret) {
  		return ret;
  	}
  	*near = v.val1;
  	return 0;
  }
  ```

## 4. `CMakeLists.txt` — **sensor file is never compiled**

- [ ] Add the source and the include dir:

  ```cmake
  target_sources(app PRIVATE
  	src/main.c
  	sensors/vl53l0x.c
  )
  target_include_directories(app PRIVATE sensors)
  ```

## 5. `src/main.c` — **sensor is never used**

- [ ] `#include "vl53l0x.h"`, call `vl53l0x_init()` next to `stepper_init()`.
- [ ] Read it in the loop and log the distance. Note the current loop is blocking for ~0.8 s
      per stepper move (200 steps × 1 ms + 500 ms sleeps), and each VL53L0X fetch blocks
      ~33 ms (single-shot ranging). For a quick test, read once per loop iteration.
      Longer term: put the sensor in its own thread (`K_THREAD_DEFINE`) so ranging and
      stepping don't stall each other.
- [ ] Decide if a failed `vl53l0x_init()` should stop the turret or just disable sensing
      (currently `stepper_init` failure makes `main` return).

## 6. Hardware / bring-up checks

- [ ] Wiring: VIN → 3V (most breakouts have a regulator, check yours), GND, SCL → PB6, SDA → PB9.
- [ ] Pull-ups: STM32 I2C needs pull-ups on SCL/SDA. Most VL53L0X breakouts include them;
      if yours doesn't, add ~4.7 kΩ to 3.3 V.
- [ ] XSHUT pin: if left floating on some bare modules the sensor won't boot. Either tie it
      high, or wire it to a free GPIO and add `xshut-gpios` to the overlay (the driver then
      handles power-up). Avoid PE9–PE12 (stepper pins).
- [ ] First boot: watch serial output for `VL53L0X_StaticInit failed` / `not ready`
      → almost always wiring, pull-ups, or XSHUT.
- [x] Pick a GPIO for the HC-SR501 PIR too (README mentions it) — add `pir-gpios` under
      `zephyr,user`, not on PE9–PE12. (Done: PE7)

## 7. Build & verify

- [ ] Pristine build after overlay/prj.conf changes:
      `west build -p always -b stm32f4_disco ~/zephyrproject/turret`
- [ ] Confirm the node made it into the devicetree:
      `grep -A5 vl53l0x build/zephyr/zephyr.dts`
- [ ] Confirm the driver got enabled: `grep VL53L0X build/zephyr/.config`
      (should show `CONFIG_VL53L0X=y`)
- [ ] `west flash --runner openocd`, then open the serial console and wave a hand in front.

## 8. Repo housekeeping

- [ ] **`build/` is committed to git.** Remove it from the repo and ignore it:
      `git rm -r --cached build && echo build/ > .gitignore`
- [ ] Empty `drivers/` directory (git doesn't track it anyway) — delete or use it.
- [ ] README: fix the sensor name spelling ("VF53L0X" → "VL53L0X"), and the `-b "board"`
      example can just be `-b stm32f4_disco`.
