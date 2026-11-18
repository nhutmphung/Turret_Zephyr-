#pragma once 
#include <zephyr/drivers/gpio.h>

struct stepper {
    const char *name; 
    struct gpio_dt_spec step; 
    struct gpio_dt_spec dir; 
};

 int stepper_init(const struct stepper *m); 
 void stepper_move(const struct stepper *m, int steps); 

