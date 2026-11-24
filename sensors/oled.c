#include "oled.h"
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/display/cfb.h>
#include <zephyr/logging/log.h> 


LOG_MODULE_REGISTER(oled, LOG_LEVEL_INF); 

static const struct device *const oled = DEVICE_DT_GET(DT_NODELABEL(oled));

static uint8_t font_h;

int oled_init(void) {
    if(!device_is_ready(oled)) {
        LOG_ERR("OLED display is not ready...");
        return -ENODEV;
    }

    // SSD1306 is monochorome, cfb needs one of the two mono formats 
    if(display_set_pixel_format(oled, PIXEL_FORMAT_MONO10) != 0 && display_set_pixel_format(oled, PIXEL_FORMAT_MONO01) != 0) {
        LOG_ERR("failed to set mono pixel format");
        return -EIO;
    }


    int ret = cfb_framebuffer_init(oled); 
    if(ret) {
        LOG_ERR("cfb init failed (%d)", ret);
        return ret;
    }

    cfb_framebuffer_clear(oled, true);
    display_blanking_off(oled); // turn the screen on

    // font 0 is the first built-in font; grab its height for line spacing
    uint8_t font_w;
    cfb_framebuffer_set_font(oled, 0);
    cfb_get_font_size(oled, 0, &font_w, &font_h);

    return 0;
}

// prints up to two lines of text, either can be NULL
void oled_print(const char *line1, const char *line2) {
    cfb_framebuffer_clear(oled, false);  // clear the buffer, don't push yet
    if (line1) {
        cfb_print(oled, line1, 0, 0);
    }
    if (line2) {
        cfb_print(oled, line2, 0, font_h);
    }
    cfb_framebuffer_finalize(oled);      // push the buffer to the screen over I2C
}                                                                             


void oled_show_coords(int xCoords, int yCoords, bool found, float distance) {
    char  line_coords[32];
    char  line_dist[32]; 

    if (found) {
        snprintf(line_coords, sizeof(line_coords), "X:%d,Y:%d ", xCoords, yCoords); 
    }
    else {
        snprintf(line_coords, sizeof(line_coords), "no coords");         
    }
    int tenths = (int)(distance * 10.0f + 0.5f);
    snprintf(line_dist, sizeof(line_dist), "%d.%d in", tenths / 10, tenths % 10);        
    oled_print(line_coords, line_dist); 
}