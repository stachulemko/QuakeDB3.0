#ifndef MVCC_H
#define MVCC_H

#include <stdint.h>

#define MAX_TRANSACTIONS 256

typedef struct {
    int32_t txn_counter;
    int8_t  txn_status[MAX_TRANSACTIONS];
} MVCC;


static inline void mvcc_init(MVCC *mvcc) {
    mvcc->txn_counter = 0;
    for (int i = 0; i < MAX_TRANSACTIONS; i++) {
        mvcc->txn_status[i] = 0; // 0 = active, 1 = committed, 2 = aborted
    }
}

static inline int32_t getTxnId(MVCC *mvcc) {
    return mvcc->txn_counter;
}

static inline void incrementTxnCounter(MVCC *mvcc) {
    mvcc->txn_counter++;
}





#endif