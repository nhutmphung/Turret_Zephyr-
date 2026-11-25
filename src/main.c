#include <stdlib.h>          // abs()
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/stepper/stepper_ctrl.h>
#include "vl53l0x.h"
#include "pir.h"
#include "oled.h"
#include "coords.h"

/* PIR pin comes from boards/stm32f4_disco.overlay (zephyr,user node) */
#define USER_NODE DT_PATH(zephyr_user)

LOG_MODULE_REGISTER(turret, LOG_LEVEL_INF);

#define CENTERX       320     // frame center for a 640x480 camera
#define CENTERY       240
#define DEADZONE_PX   10      // ignore errors smaller than this (px)
#define MAX_PAN       2500.0f // pan speed limit (steps/s), like setMaxSpeed(2500)
#define MAX_TILT      700.0f  // tilt speed limit (steps/s)
#define MIN_SPS       20.0f   // slower than this counts as "stop"
#define COORDS_TIMEOUT_MS 300 // no coords for this long -> stop both motors

// gains are always POSITIVE now: speed = Kp * |error|.
// the direction comes from the left/right/up/down mapping below, not from the sign of Kp.
static float Kp_PAN  = 15.0f;  // steps/s per pixel of error
static float Kp_TILT = 4.0f;   // keep POSITIVE: flip direction with TILT_UP/TILT_DOWN instead

// which DIR pin level means "left" / "up". if the motor turns the wrong way
// when the log says LEFT, swap NEGATIVE and POSITIVE here (and only here).
#define PAN_LEFT   STEPPER_CTRL_DIRECTION_POSITIVE
#define PAN_RIGHT  STEPPER_CTRL_DIRECTION_NEGATIVE
#define TILT_UP    STEPPER_CTRL_DIRECTION_POSITIVE
#define TILT_DOWN  STEPPER_CTRL_DIRECTION_NEGATIVE

static bool oled_ok;   // set by startup_seq(), used in the main loop

static const struct device *const pan_dev  = DEVICE_DT_GET(DT_NODELABEL(pan_ctrl));
static const struct device *const tilt_dev = DEVICE_DT_GET(DT_NODELABEL(tilt_ctrl));

// run one motor in a direction at sps steps/s (always positive).
// the controller keeps stepping in the background until it's told otherwise.
static int motor_run(const struct device *dev, enum stepper_ctrl_direction dir,
		     float sps, float max_sps)
{
	sps = MIN(sps, max_sps);
	if (sps < MIN_SPS) {
		return stepper_ctrl_stop(dev);
	}

	int ret = stepper_ctrl_set_microstep_interval(dev, (uint64_t)(NSEC_PER_SEC / sps));
	if (ret < 0) {
		return ret;
	}
	return stepper_ctrl_run(dev, dir);
}

static void pan_left(float sps)   { motor_run(pan_dev,  PAN_LEFT,  sps, MAX_PAN); }
static void pan_right(float sps)  { motor_run(pan_dev,  PAN_RIGHT, sps, MAX_PAN); }
static void pan_stop(void)        { stepper_ctrl_stop(pan_dev); }
static void tilt_up(float sps)    { motor_run(tilt_dev, TILT_UP,   sps, MAX_TILT); }
static void tilt_down(float sps)  { motor_run(tilt_dev, TILT_DOWN, sps, MAX_TILT); }
static void tilt_stop(void)       { stepper_ctrl_stop(tilt_dev); }

static const struct pir_sensor pir = {
    .name = "pir",
    .pin = GPIO_DT_SPEC_GET(USER_NODE, pir_gpios),
};

// brings up every peripheral. returns 0 if the turret can run, negative if not.
static int startup_seq(void)
{
	if (!device_is_ready(pan_dev) || !device_is_ready(tilt_dev)) {
		LOG_ERR("stepper controllers not ready");
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

	/* direction check: watch the turret and make sure each move matches the log.
	 * remove this once the directions are confirmed. */
	LOG_INF("dir test: pan LEFT");
	pan_left(500.0f);  k_msleep(1000); pan_stop(); k_msleep(500);
	LOG_INF("dir test: pan RIGHT");
	pan_right(500.0f); k_msleep(1000); pan_stop(); k_msleep(500);
	LOG_INF("dir test: tilt UP");
	tilt_up(600.0f);   k_msleep(800);  tilt_stop(); k_msleep(500);
	LOG_INF("dir test: tilt DOWN");
	tilt_down(600.0f); k_msleep(800);  tilt_stop();

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
	int64_t last_coords_ms = 0;               // time of the last coords message

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

		// track: on every new message, set a speed. the motors keep running at that
		// speed between messages (like runSpeed() on the Arduino)
		if (new_coords) {
			last_coords_ms = k_uptime_get();

			if (tgt.found) {
				int errX = tgt.x - CENTERX;   // <0: left of center, >0: right of center
				int errY = tgt.y - CENTERY;   // <0: above center,   >0: below center
				const char *pan_cmd;
				const char *tilt_cmd;

				// ---- pan ----
				if (errX < -DEADZONE_PX) {          // face is left of center
					pan_left(Kp_PAN * -errX);
					pan_cmd = "LEFT";
				} else if (errX > DEADZONE_PX) {    // face is right of center
					pan_right(Kp_PAN * errX);
					pan_cmd = "RIGHT";
				} else {
					pan_stop();
					pan_cmd = "STOP";
				}

				// ---- tilt ----
				if (errY < -DEADZONE_PX) {          // face is above center
					tilt_up(Kp_TILT * -errY);
					tilt_cmd = "UP";
				} else if (errY > DEADZONE_PX) {    // face is below center
					tilt_down(Kp_TILT * errY);
					tilt_cmd = "DOWN";
				} else {
					tilt_stop();
					tilt_cmd = "STOP";
				}

				LOG_INF("errX %d -> %s %d sps | errY %d -> %s %d sps",
					errX, pan_cmd, (int)(Kp_PAN * abs(errX)),
					errY, tilt_cmd, (int)(Kp_TILT * abs(errY)));
			} else {
				// python sent NONE: same as the Arduino "S"
				pan_stop();
				tilt_stop();
			}
		}

		// safety: velocity mode runs until told otherwise, so stop if python goes quiet
		if (k_uptime_get() - last_coords_ms > COORDS_TIMEOUT_MS) {
			pan_stop();
			tilt_stop();
		}

		k_msleep(10);   // short sleep so speed updates keep up with the camera
	}

	return 0;
}
