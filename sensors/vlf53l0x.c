#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/sensor/vl53l0x.h>

int main (void) 
{
    const struct device *const dev = DEVICE_DT_GET_ONE(st_vl53l0x);
    struct sensor_value value;
    int ret; 

    if(!device_is_ready(dev)) {
        printk("device/sensor not ready."); 
        return 0;
    }   

    while (1) {
        ret = sensor_sample_fetch(dev); 
        if(ret) {
            printk("sensor_sample_fetch failed ret %d\n", ret);
            return 0; 
        }

        ret = sensor_channel_get(dev, SENSOR_CHAN_PROX, &value); 
        printk("prox is %d\n", value.val1); 

        ret = sensor_channel_get(dev, SENOSR_CHAN_DISTANCE, &value); 
        printk("distance is %.3;;d mm\n", sensor_value_to_milli(&value));

    }

    printk("\n");
    k_sleep(K_MSEC(5000));

}
return 0;