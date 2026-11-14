#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h> 

static const struct gpio_dt_spec step = GPIO_DT_SPEC_GET(DT_NODELABEL(stepper0), step_gpios);
static const struct gpio_dt_spec dir  = GPIO_DT_SPEC_GET(DT_NODELABEL(stepper0), dir_gpios);

int main (void) 
{
gpio_pin_configure_dt(&step, GPIO_OUTPUT_INACTIVE); 
gpio_pin_configure_dt(&dir, GPIO_OUTPUT_INACTIVE); 

while (1) {
gpio_pin_set_dt(&step, 1);
k_busy_wait(2);
gpio_pin_configure_dt(&dir, 1); 
k_usleep(1000);
}

}