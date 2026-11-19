#pragma once 
#include <stdbool.h>

int vl53l0x_init(void); 
float vl53l0x_read_in(float *inch, bool *near); 
