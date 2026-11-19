#pragma once 

#include <zephyr/drivers/gpio.h>
#include <stdbool.h>

struct pir_sensor {
    const char *name; 
    struct gpio_dt_spec pin; 
};


int pir_init(const struct pir_sensor *m); 
bool pir_detect(const struct pir_sensor *m); 

