#include "pir.h"
#include <zephyr/logging/log.h> 

LOG_MODULE_REGISTER(pir, LOG_LEVEL_INF); 

int pir_init(const struct pir_sensor *p) {
     if (!gpio_is_ready_dt(&p->pin)) {
        LOG_ERR("%s: GPIO port not ready", p->name);
        return -ENODEV;
    }

    int ret = gpio_pin_configure_dt(&p->pin, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("%s: pin config failed (%d)", p->name, ret);
        return ret;
    }

    return 0;
}

bool pir_detect(const struct pir_sensor *p) {

    return gpio_pin_get_dt(&p->pin) > 0; 
}