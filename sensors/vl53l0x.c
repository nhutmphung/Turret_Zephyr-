#include "vl53l0x.h"
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h> 
#include <zephyr/device.h>

LOG_MODULE_REGISTER(tof, LOG_LEVEL_INF);

static const struct device *const tof = DEVICE_DT_GET(DT_NODELABEL(tof));

int vl53l0x_init(void) {
    if(!device_is_ready(tof)) {
        LOG_ERR("vl53l0x is not ready...");
        return -ENODEV;
    }
return 0;
}

// uses the zephyr vl53l0x api to get the distance 
// ret = return value, if it returns 0 then YAY success
// the function basically gets the distnace, then converts to mm, then sends to main.c

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