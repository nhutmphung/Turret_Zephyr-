#pragma once 
#include <stdbool.h>

// one parsed message from the python script
struct coords {
    bool found;  // false when python sent "NONE"
    int x;       // pixel coords of the target center (only valid if found)
    int y;
};

int coords_init(void);
bool coords_get(struct coords *c);
