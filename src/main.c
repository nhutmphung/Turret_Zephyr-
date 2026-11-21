#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "vl53l0x.h"
#include "stepper.h"
#include "pir.h"
#include "oled.h"
#include "coords.h"

/* Pins come from boards/stm32f4_disco.overlay (zephyr,user node) */
#define USER_NODE DT_PATH(zephyr_user)

LOG_MODULE_REGISTER(turret, LOG_LEVEL_INF);
#define STEPS_PER_MOVE 2000    /* one revolution on a 1.8 deg motor, full step */

static bool oled_ok;   // set by startup_seq(), used in the main loop

static const struct stepper pan = {
	.name = "pan",
	.step = GPIO_DT_SPEC_GET(USER_NODE, pan_step_gpios),
	.dir  = GPIO_DT_SPEC_GET(USER_NODE, pan_dir_gpios),
};

static const struct stepper tilt = {
	.name = "tilt",
	.step = GPIO_DT_SPEC_GET(USER_NODE, tilt_step_gpios),
	.dir  = GPIO_DT_SPEC_GET(USER_NODE, tilt_dir_gpios),
};

static const struct pir_sensor pir = {
    .name = "pir",
    .pin = GPIO_DT_SPEC_GET(USER_NODE, pir_gpios),
};

// brings up every peripheral. returns 0 if the turret can run, negative if not.
static int startup_seq(void)
{
	if (stepper_init(&pan) < 0 || stepper_init(&tilt) < 0) {
		return -ENODEV;
	}
	LOG_INF("Steppers ready.");

	if (vl53l0x_init() < 0) {
		return -ENODEV;
	}
	LOG_INF("ToF sensor ready.");

	if (pir_init(&pir) < 0) {
		return -ENODEV;
	}
	LOG_INF("PIR sensor ready.");

	/* the screen is nice-to-have: if it fails, keep the turret running */
	oled_ok = (oled_init() == 0);   // no "bool" here: assigns the global instead of making a new local
	if (oled_ok) {
		LOG_INF("OLED ready.");
		oled_print("turret", "starting...");
	} else {
		LOG_WRN("OLED not found, continuing without display");
	}

	if (coords_init() < 0) {
		return -ENODEV;
	}
	LOG_INF("Coords UART ready.");

	LOG_INF("turret ready");
	return 0;
}

int main(void)
{
	if (startup_seq() < 0) {
		LOG_ERR("startup failed, stopping");
		return 0;
	}

	/* Test pattern: sweep each axis forward and back */
	while (1) {

		struct coords tgt;
		if (coords_get(&tgt)) {
			if (tgt.found) {
				LOG_INF("target x=%d y=%d", tgt.x, tgt.y);
			} else {
				LOG_INF("no target");
			}
		}

		float inch;
		bool near;

		int ret = vl53l0x_read_in(&inch, &near);
		if (ret == 0) {
			LOG_INF("distance %.1f in, near = %d", (double)inch, near);
			if (oled_ok) {
				oled_show_distance(inch, near);
			}
		} else if (oled_ok) {
			oled_print("ToF error", NULL);
		}

        if( pir_detect(&pir)) {
            LOG_INF("motion detected");
        }  
        /*
		LOG_INF("pan +%d", 5000);
		stepper_move(&pan, 5000);
		k_msleep(100);

		LOG_INF("pan -%d", 5000);
		stepper_move(&pan, -5000);
		k_msleep(100);

		LOG_INF("tilt +%d", STEPS_PER_MOVE);
		stepper_move(&tilt, STEPS_PER_MOVE);
		k_msleep(100);

		LOG_INF("tilt -%d", STEPS_PER_MOVE);
		stepper_move(&tilt, -STEPS_PER_MOVE);
		k_msleep(100);
        */

		k_msleep(50);   // keeps the loop from flooding the serial log
	}

	return 0;
}
