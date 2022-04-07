#include <stdbool.h>
#include <ch.h>

void panic(const char *fmt, ...) {
    chSysHalt(fmt);
}
