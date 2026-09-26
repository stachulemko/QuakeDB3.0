#ifndef LOG_H
#define LOG_H
#include <stdio.h>

/*
 * Log levels — set before including or via -DLOG_LEVEL=N
 *
 *   0 — silent (nothing is printed)
 *   1 — ERROR only
 *   2 — ERROR + DEBUG  (default)
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
