#pragma once
#include <stdbool.h>

int oled_init(void);
void oled_print(const char *line1, const char *line2);
void oled_show_coords(int xCoords, int yCoords, bool found, float distance);
