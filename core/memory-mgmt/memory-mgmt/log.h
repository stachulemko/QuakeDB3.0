#ifndef LOG_H
#define LOG_H
#include <stdio.h>

/*
 * Poziomy logowania — ustaw przed includowaniem lub przez -DLOG_LEVEL=N
 *
 *   0 — cisza (nic nie jest wypisywane)
 *   1 — tylko ERROR
 *   2 — ERROR + DEBUG  (domyślnie)
 */
#ifndef LOG_LEVEL
#define LOG_LEVEL 2
#endif

#if LOG_LEVEL >= 1
#define LOG_ERROR(...) fprintf(stderr, "[ERROR] " __VA_ARGS__)
#else
#define LOG_ERROR(...) ((void)0)
#endif

#if LOG_LEVEL >= 2
#define LOG_DEBUG(...) fprintf(stderr, "[DEBUG] " __VA_ARGS__)
#else
#define LOG_DEBUG(...) ((void)0)
#endif

#endif
