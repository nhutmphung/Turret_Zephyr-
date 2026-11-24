#include <stdlib.h>          // abs()
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
#define STEPS_PER_MOVE 2000    // one revolution on a 1.8 deg motor, full step 

#define CENTERX       320     // frame center for a 640x480 camera
#define CENTERY       240
#define DEADZONE_PX   10      // ignore errors smaller than this (px)
#define MAX_STEPS     200     // most steps allowed in one correction

static float Kp_PAN  = 1.0f;  // steps per pixel. start low, tune up (negative flips direction)
static float Kp_TILT = 1.0f;

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

	struct coords tgt = { .found = false };   // newest target, kept between loop passes

	while (1) {
		float inch = 0.0f;
		bool near = false;

		int ret = vl53l0x_read_in(&inch, &near);

		bool new_coords = coords_get(&tgt);   // true only on passes where a new message arrived
		if (new_coords) {
			if (tgt.found) {
				LOG_INF("target x=%d y=%d", tgt.x, tgt.y);
			} else {
				LOG_INF("no target");
			}
		}

		if (oled_ok) {
			oled_show_coords(tgt.x, tgt.y, tgt.found, inch);
		}

		if (ret == 0) {
			//LOG_INF("distance %.1f in, near = %d", (double)inch, near);
		}

		if (pir_detect(&pir)) {
			LOG_INF("motion detected");
		}

		// track: only on a NEW message that actually has a target,
		// so the same correction is never applied twice
		if (new_coords && tgt.found) {
			int errX = tgt.x - CENTERX;
			int errY = tgt.y - CENTERY;

			if (abs(errX) > DEADZONE_PX) {
				int stepsX = CLAMP((int)(errX * Kp_PAN), -MAX_STEPS, MAX_STEPS);
				stepper_move(&pan, stepsX*100);
                LOG_INF("steperY move : %d", stepsX);
			}
			if (abs(errY) > DEADZONE_PX) {
				int stepsY = CLAMP((int)(errY * Kp_TILT), -MAX_STEPS, MAX_STEPS);
				stepper_move(&tilt, stepsY*100);
                LOG_INF("steperX move : %d", stepsY);

			}
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
