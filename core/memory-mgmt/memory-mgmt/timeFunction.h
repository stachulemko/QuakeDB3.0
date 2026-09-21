//
// Created by stas on 21.09.2026.
//

#ifndef QUAKEDB3_0_TIMEFUNCTION_H
#define QUAKEDB3_0_TIMEFUNCTION_H
#include <time.h>

static long long nowTime(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}
#endif //QUAKEDB3_0_TIMEFUNCTION_H
