# TODO: get the VL53L0X ToF sensor building and running

Current state: `sensors/vlf53l0x.c` exists but **is not compiled** (it's not in `CMakeLists.txt`),
and there's **no devicetree node** for the sensor. So the build passes now only because the
sensor code is never touched. Once it's wired in, the items below will each break the build or the run.

Checked against the Zephyr tree in `~/zephyrproject/zephyr`
(`dts/bindings/sensor/st,vl53l0x.yaml`, `drivers/sensor/st/vl53l0x/`).

---

## Tomorrow (2026-11-18)

### A. Wire the ToF sensor to I2C1
The overlay node (`tof: vl53l0x@29` on `&i2c1`) is already in place, so this is just hardware.

- [ ] VL53L0X **VIN → 3V**, **GND → GND** (check whether your breakout has its own regulator)
- [ ] **SCL → PB6**, **SDA → PB9**
- [ ] Check for pull-ups on SCL/SDA (most breakouts have them; if not, ~4.7 kΩ to 3.3 V)
- [ ] XSHUT: tie it high, or wire it to a free GPIO (not PE9–PE12)
- [ ] Flash and check the serial log for `not ready` / `StaticInit failed`

### B. Add a GPIO for the HC-SR501 PIR
- [ ] Pick a free pin. Suggested: **PE7**. Avoid PE9–PE12 (steppers), PB6/PB9 (I2C),
      PA2/PA3 (console UART), PA0 (user button), PD12–PD15 (LEDs), PA5–PA7/PE3/PE0/PE1 (on-board accelerometer).
- [ ] Wiring: PIR **VCC → 5V**, **GND → GND**, **OUT → PE7** (OUT is 3.3 V logic, safe for the STM32)
- [ ] Add it under `zephyr,user` in `boards/stm32f4_disco.overlay`:

  ```dts
  pir-gpios = <&gpioe 7 (GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN)>;
  ```

- [ ] In code: `GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), pir_gpios)`, configure it as `GPIO_INPUT`,
      and either poll it in the loop or set up an edge interrupt (`GPIO_INT_EDGE_RISING`)
- [ ] Note: the HC-SR501 needs ~30–60 s to settle after power-up, so ignore triggers right after boot

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
- [ ] Pick a GPIO for the HC-SR501 PIR too (README mentions it) — add `pir-gpios` under
      `zephyr,user`, not on PE9–PE12.

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
