//
// Created by stas on 21.09.2026.
//
#include <time.h>

static long long teraz_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}