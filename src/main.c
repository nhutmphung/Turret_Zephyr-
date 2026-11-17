#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(turret, LOG_LEVEL_INF);

/* Pins come from boards/stm32f4_disco.overlay (zephyr,user node) */
#define USER_NODE DT_PATH(zephyr_user)

#define STEP_PULSE_US    2      /* high time of a step pulse */
#define STEP_PERIOD_US   1000   /* time between steps (~1000 steps/s) */
#define STEPS_PER_MOVE   200    /* one revolution on a 1.8 deg motor, full step */

struct stepper {
	const char *name;
	struct gpio_dt_spec step;
	struct gpio_dt_spec dir;
};

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

static int stepper_init(const struct stepper *m)
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
static void stepper_move(const struct stepper *m, int steps)
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

int main(void)
{
	if (stepper_init(&pan) < 0 || stepper_init(&tilt) < 0) {
		return 0;
	}

	LOG_INF("turret ready");

	/* Test pattern: sweep each axis forward and back */
	while (1) {
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
