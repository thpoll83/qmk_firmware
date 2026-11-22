#include "polykybd.h"

#define PK_MIN(x, y) (((x) < (y)) ? (x) : (y))
#define PK_MAX(x, y) (((x) > (y)) ? (x) : (y))
#define PK_ABS(x) ((x) < 0 ? (-(x)) : (x))
#define PK_POW(val, x, n) \
    val = 1; \
    for (uint8_t i = 0; i < n; ++i) { val *= x;}



void peripherals_reset(void);
