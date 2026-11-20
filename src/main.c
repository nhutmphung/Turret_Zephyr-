#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "vl53l0x.h"
#include "stepper.h"
#include "pir.h"
#include "oled.h"

/* Pins come from boards/stm32f4_disco.overlay (zephyr,user node) */
#define USER_NODE DT_PATH(zephyr_user)

LOG_MODULE_REGISTER(turret, LOG_LEVEL_INF);
#define STEPS_PER_MOVE 2000    /* one revolution on a 1.8 deg motor, full step */

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

int main(void)
{
	if (stepper_init(&pan) < 0 || stepper_init(&tilt) < 0) {
		return 0;
	}
    LOG_INF("Steppers is ready.");
    if(vl53l0x_init() < 0) {
        return 0;
    }
    LOG_INF("ToF sensor is ready.");

    if(pir_init(&pir) < 0) {
        return 0;
    }
    LOG_INF("PIR sensor is ready.");

	/* the screen is nice-to-have: if it fails, keep the turret running */
	bool oled_ok = (oled_init() == 0);
	if (oled_ok) {
		LOG_INF("OLED ready.");
		oled_print("turret", "starting...");
	} else {
		LOG_WRN("OLED not found, continuing without display");
	}

	LOG_INF("turret ready");

	/* Test pattern: sweep each axis forward and back */
	while (1) {

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
	}

	return 0;
}
