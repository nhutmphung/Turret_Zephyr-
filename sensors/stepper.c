#include "stepper.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(stepper, LOG_LEVEL_INF);


#define STEP_PULSE_US    2      /* high time of a step pulse */
#define STEP_PERIOD_US   1000   /* time between steps (~1000 steps/s) */

 int stepper_init(const struct stepper *m)
{
	int ret;

	if (!gpio_is_ready_dt(&m->step) || !gpio_is_ready_dt(&m->dir)) {
		LOG_ERR("%s: GPIO port not ready", m->name);
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&m->step, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("%s: step pin config failed (%d)", m->name, ret);
		return ret;
	}

	ret = gpio_pin_configure_dt(&m->dir, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("%s: dir pin config failed (%d)", m->name, ret);
		return ret;
	}

	return 0;
}


/* Move a motor a number of steps. Positive = one direction, negative = the other. */
void stepper_move(const struct stepper *m, int steps)
{
	gpio_pin_set_dt(&m->dir, steps >= 0 ? 1 : 0);
	k_busy_wait(5); /* let DIR settle before the first step */

	if (steps < 0) {
		steps = -steps;
	}

	for (int i = 0; i < steps; i++) {
		gpio_pin_set_dt(&m->step, 1);   /* rising edge = one step */
		k_busy_wait(STEP_PULSE_US);
		gpio_pin_set_dt(&m->step, 0);
		k_usleep(STEP_PERIOD_US - STEP_PULSE_US);
	}
}