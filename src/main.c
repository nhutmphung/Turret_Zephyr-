#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "vl53l0x.h"
#include "stepper.h"

/* Pins come from boards/stm32f4_disco.overlay (zephyr,user node) */
#define USER_NODE DT_PATH(zephyr_user)

LOG_MODULE_REGISTER(turret, LOG_LEVEL_INF);
#define STEPS_PER_MOVE   200    /* one revolution on a 1.8 deg motor, full step */

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

int main(void)
{
	if (stepper_init(&pan) < 0 || stepper_init(&tilt) < 0) {
		return 0;
	}

    if(vl53l0x_init() < 0) {
        return 0;
    }
	LOG_INF("turret ready");

	/* Test pattern: sweep each axis forward and back */
	while (1) {

        int mm; 
        bool near; 

        int ret = vl53l0x_read_mm(&mm, &near); 
        if (ret == 0) {
            LOG_INF("distance %d mm, near = %d", mm, near); 
        }

		LOG_INF("pan +%d", STEPS_PER_MOVE);
		stepper_move(&pan, STEPS_PER_MOVE);
		k_msleep(500);

		LOG_INF("pan -%d", STEPS_PER_MOVE);
		stepper_move(&pan, -STEPS_PER_MOVE);
		k_msleep(500);

		LOG_INF("tilt +%d", STEPS_PER_MOVE);
		stepper_move(&tilt, STEPS_PER_MOVE);
		k_msleep(500);

		LOG_INF("tilt -%d", STEPS_PER_MOVE);
		stepper_move(&tilt, -STEPS_PER_MOVE);
		k_msleep(500);
	}

	return 0;
}
