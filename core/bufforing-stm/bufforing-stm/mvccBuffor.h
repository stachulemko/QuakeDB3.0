#ifndef MVCC_BUFFOR_H
#define MVCC_BUFFOR_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "mvcc.h"

/*
 * MVCCBuffor — sliding window over transaction statuses stored on disk.
 *
 * When txn_counter exceeds MAX_TRANSACTIONS the in-memory MVCC array can no
 * longer hold all statuses.  Old statuses are flushed to a binary file (one
 * int8_t per transaction, at byte offset == xid).  MVCCBuffor loads a window
 * of MAX_TRANSACTIONS entries from that file, positioned so that the requested
 * xid is the *last* entry in the buffer.
 *
 *   start_txn                          start_txn + MAX_TRANSACTIONS - 1
 *       |                                          |
 *       v                                          v
 *       [ txn_status[0] ... txn_status[MAX_TRANSACTIONS-1] ]
 */

typedef struct {
    int32_t start_txn;
    int8_t  txn_status[MAX_TRANSACTIONS];
} MVCCBuffor;

static inline void mvcc_buffor_init(MVCCBuffor *buffor) {
    buffor->start_txn = 0;
    memset(buffor->txn_status, 0, sizeof(buffor->txn_status));
}

static inline void create_MVCCBuffor(MVCCBuffor **buffor) {
    *buffor = (MVCCBuffor *)calloc(1, sizeof(MVCCBuffor));
}

/* ── persist ─────────────────────────────────────────────────────────── */

/*
 * Save `count` status bytes to `filepath` starting at file offset `base_txn`.
 * Creates the file if it does not exist.  Returns 0 on success, -1 on error.
 */
static inline int mvcc_buffor_save(const int8_t *txn_status,
                                   const char   *filepath,
                                   int32_t       base_txn,
                                   int32_t       count)
{
    FILE *f = fopen(filepath, "r+b");
    if (!f) {
        f = fopen(filepath, "wb");
        if (!f) return -1;
    }
    if (fseek(f, (long)base_txn, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    size_t written = fwrite(txn_status, sizeof(int8_t), (size_t)count, f);
    fclose(f);
    return ((int32_t)written == count) ? 0 : -1;
}

/*
 * Convenience: flush the whole in-memory MVCC status array to file.
 * `base_txn` is the xid that txn_status[0] represents (usually 0 for the
 * first batch, MAX_TRANSACTIONS for the second, etc.).
 */
static inline int mvcc_buffor_flush(MVCC        *mvcc,
                                    const char  *filepath,
                                    int32_t      base_txn)
{
    int32_t count = mvcc->txn_counter - base_txn;
    if (count <= 0) return 0;
    if (count > MAX_TRANSACTIONS) count = MAX_TRANSACTIONS;
    return mvcc_buffor_save(mvcc->txn_status, filepath, base_txn, count);
}

/* ── load ────────────────────────────────────────────────────────────── */

/*
 * Load a window from `filepath` into `buffor` so that `target_xid` is the
 * last populated entry.
 *
 *   start_txn = max(0, target_xid - MAX_TRANSACTIONS + 1)
 *
 * Returns 0 on success, -1 on error (file missing, target_xid not in file).
 */
static inline int mvcc_buffor_load(MVCCBuffor *buffor,
                                   const char *filepath,
                                   int32_t     target_xid)
{
    int32_t start = target_xid - MAX_TRANSACTIONS + 1;
    if (start < 0) start = 0;
    buffor->start_txn = start;
    memset(buffor->txn_status, 0, sizeof(buffor->txn_status));

    FILE *f = fopen(filepath, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);

    if (file_size <= (long)start) {
        fclose(f);
        return -1;
    }

    if (fseek(f, (long)start, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }

    int32_t to_read = target_xid - start + 1;
    if (to_read > MAX_TRANSACTIONS)       to_read = MAX_TRANSACTIONS;
    if (start + to_read > (int32_t)file_size) to_read = (int32_t)(file_size - start);

    size_t n = fread(buffor->txn_status, sizeof(int8_t), (size_t)to_read, f);
    fclose(f);
    return ((int32_t)n == to_read) ? 0 : -1;
}

/* ── query ───────────────────────────────────────────────────────────── */

/*
 * Return the status of `xid` from the buffer, or -1 if xid is outside the
 * currently loaded window.
 */
static inline int8_t mvcc_buffor_get_status(const MVCCBuffor *buffor,
                                            int32_t           xid)
{
    if (xid < buffor->start_txn ||
        xid >= buffor->start_txn + MAX_TRANSACTIONS) {
        return -1;
    }
    return buffor->txn_status[xid - buffor->start_txn];
}

/*
 * Unified getter — returns status of `xid`:
 *   • If xid is within the current in-memory MVCC window
 *     (base_txn..base_txn+MAX_TRANSACTIONS-1 and < txn_counter), read directly.
 *   • Otherwise load from file into `buffor` and read from there.
 *   • Returns -1 on any error.
 */
static inline int8_t mvcc_get_txn_status(MVCC       *mvcc,
                                         MVCCBuffor *buffor,
                                         const char *filepath,
                                         int32_t     xid)
{
    if (xid < 0) return -1;

    if (xid >= mvcc->base_txn &&
        xid <  mvcc->base_txn + MAX_TRANSACTIONS &&
        xid <  mvcc->txn_counter) {
        return mvcc->txn_status[xid - mvcc->base_txn];
    }

    const char *path = filepath ? filepath : mvcc->filepath;
    if (!path) return -1;

    if (mvcc_buffor_load(buffor, path, xid) != 0) {
        return -1;
    }
    return mvcc_buffor_get_status(buffor, xid);
}

#endif /* MVCC_BUFFOR_H */