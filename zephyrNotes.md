## Adding a new peripheral / device

Every new device touches the same 5 places, in this order:

| # | File | What it does | What to add |
|---|------|--------------|-------------|
| 1 | `boards/stm32f4_disco.overlay` | Tells Zephyr **what** the hardware is and **where** it's connected | **Bus device** (I2C/SPI): a node under the bus with `compatible` + `reg` (address)<br>**Plain GPIO device** (PIR, LED, button): a `xxx-gpios` property under `zephyr,user` |
| 2 | `prj.conf` | **Turns on** the subsystems | `CONFIG_I2C=y`, `CONFIG_SPI=y`, `CONFIG_SENSOR=y`, `CONFIG_GPIO=y`, etc. The driver itself turns on automatically once its devicetree node exists. Only set driver options (like thresholds) here. |
| 3 | `sensors/<device>.c` + `.h` | Says **how** to use the device | `xxx_init()` and `xxx_read()` functions. **No `main()`**: only `src/main.c` has one. |
| 4 | `CMakeLists.txt` | **Compiles** the new file | Add the `.c` to `target_sources(...)`. Make sure `target_include_directories(app PRIVATE sensors)` is there (only needed once). |
| 5 | `src/main.c` | Says **when** to use the device | `#include "<device>.h"`, call `xxx_init()` at startup and `xxx_read()` in the loop |

### Copy-paste examples

**Overlay: I2C device**
```dts
&i2c1 {
	mydev: mydev@29 {
		compatible = "st,vl53l0x";
		reg = <0x29>;
	};
};
```

**Overlay: GPIO pin** (name must end in `-gpios`)
```dts
/ {
	zephyr,user {
		pir-gpios = <&gpioe 13 GPIO_ACTIVE_HIGH>;
	};
};
```

**Getting the device in C**

| Device type | Code |
|-------------|------|
| Bus device (by compatible) | `DEVICE_DT_GET_ONE(st_vl53l0x)` |
| Bus device (by label) | `DEVICE_DT_GET(DT_NODELABEL(mydev))` |
| GPIO pin | `GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), pir_gpios)` |
| Ready check (always do this in init) | `device_is_ready(dev)` / `gpio_is_ready_dt(&spec)` |

**CMakeLists.txt**
```cmake
target_sources(app PRIVATE
	src/main.c
	sensors/vl53l0x.c
	sensors/pir.c
)
target_include_directories(app PRIVATE sensors)
```

### Gotchas

| Problem | Cause / fix |
|---------|-------------|
| Kconfig error about unsatisfied dependencies | You set a driver option in `prj.conf` but the devicetree node doesn't exist yet. Add the node first. |
| `multiple definition of 'main'` | A file other than `src/main.c` has a `main()`. Turn it into `init`/`read` functions. |
| `__device_dts_ord_...` undefined | No devicetree node matches the `compatible` / label you used in C. |
| Don't know the `compatible` string or required properties | Look it up: `grep -r "<part name>" ~/zephyrproject/zephyr/dts/bindings` |
| Bus doesn't work | Check that the bus (`&i2c1`, `&spi1`, ...) has `status = "okay"` and pins assigned in the board `.dts` or your overlay. |
| Overlay / `prj.conf` changes not taking effect | Do a pristine build: `west build -p always -b stm32f4_disco ~/zephyrproject/turret` |

> **Rule of thumb:** overlay = *what and where* · `prj.conf` = *turn it on* · `.c` = *how* · CMake = *compile it* · `main.c` = *when*
