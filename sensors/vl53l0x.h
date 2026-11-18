#pragma once 
#include <stdbool.h>

int vl53l0x_init(void); 
int vl53l0x_read_mm(int *mm, bool *near); 
