#ifndef MVCC_H
#define MVCC_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../memory-mgmt/memory-mgmt/config.h"
#include "../../xid32Recycler/xid32Recycler.h"

#define MAX_TRANSACTIONS 256

/*
 * this structure contain txn counter icremented always when new transaction is started(begin transaction) - txn_counter
 * base_txn conatin min val of xid to know what transaction are in txn_status which is are buffor for transaction .
 * txn_status conatin state of transaction from wanted range .
*/
typedef struct {
    int32_t txn_counter;
    int32_t base_txn;
    int8_t  txn_status[MAX_TRANSACTIONS];
    const char *filepath;
} MVCC;

void create_MVCC(MVCC **mvcc) {
    *mvcc = (MVCC *) calloc(1,sizeof(MVCC));
}

static inline void mvcc_init(MVCC *mvcc) {
    mvcc->txn_counter = 0;
    mvcc->base_txn = 0;
    mvcc->filepath = NULL;
    for (int i = 0; i < MAX_TRANSACTIONS; i++) {
        mvcc->txn_status[i] = 0; // 0 = active, 1 = committed, 2 = aborted
    }
}

static inline void mvcc_init_with_file(MVCC *mvcc, const char *filepath) {
    mvcc_init(mvcc);
    mvcc->filepath = filepath;
}

static inline int32_t getTxnId(MVCC *mvcc) {
    return mvcc->txn_counter;
}

/* the counter wraps around after 2^32 xids and skips the reserved values (xid32Recycler.h) */
static inline int32_t getAndIcrement(MVCC *mvcc) {
    int32_t xid = mvcc->txn_counter;
    mvcc->txn_counter = xidNext(xid);
    return xid;
}

static inline void incrementTxnCounter(MVCC *mvcc) {
    mvcc->txn_counter = xidNext(mvcc->txn_counter);
}

/* tests: jump the counter to `xid` (like pg_resetwal -x) with an empty status window */
static inline void mvcc_set_next_xid(MVCC *mvcc, int32_t xid) {
    mvcc->txn_counter = xid;
    mvcc->base_txn = (int32_t)((uint32_t)xid & ~(uint32_t)(MAX_TRANSACTIONS - 1));
    memset(mvcc->txn_status, 0, sizeof(mvcc->txn_status));
}









#endif