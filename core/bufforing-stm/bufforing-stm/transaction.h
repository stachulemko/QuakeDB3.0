//
// Created by stas on 16.04.2026.
//

#ifndef QUAKEDB3_0_TRANSACTION_H
#define QUAKEDB3_0_TRANSACTION_H

#include "mvccBuffor.h"
#include "../../memory-mgmt/memory-mgmt/timeFunction.h"

#define TXN_ACTIVE 0
#define TXN_COMMITED 1
#define TXN_ABORTED 2



#define ISOLATION_REPEATABLE_READ 1
#define ISOLATION_READ_COMMITTED  2

typedef struct {
    int32_t xid;
    int8_t  isolation_level;
    int64_t startTime;
}Transaction;

/*
 * When the in-memory window is full, flush it to file and advance base_txn.
 * Called automatically by beginTransaction / beginTransactionWithIsolation.
 */
static inline void mvcc_flush_if_full(MVCC *mvcc) {
    if (mvcc->txn_counter - mvcc->base_txn >= MAX_TRANSACTIONS) {
        if (mvcc->filepath) {
            mvcc_buffor_save(mvcc->txn_status, mvcc->filepath,
                             mvcc->base_txn, MAX_TRANSACTIONS);
        }
        memset(mvcc->txn_status, 0, sizeof(mvcc->txn_status));
        mvcc->base_txn += MAX_TRANSACTIONS;
    }
}

/*
 * Set txn_status for a given xid — in-memory if in current window,
 * directly to file if in an older (already flushed) batch.
 */
static inline void mvcc_set_txn_status(MVCC *mvcc, int32_t xid, int8_t status) {
    if (xid >= mvcc->base_txn && xid < mvcc->base_txn + MAX_TRANSACTIONS) {
        mvcc->txn_status[xid - mvcc->base_txn] = status;
    } else if (mvcc->filepath && xid >= 0 && xid < mvcc->base_txn) {
        mvcc_buffor_save(&status, mvcc->filepath, xid, 1);
    }
}


void beginTransaction(MVCC *mvcc,Transaction *transaction) {
    mvcc_flush_if_full(mvcc);
    transaction->xid = getTxnId(mvcc);
    transaction->isolation_level = VIEW_MODE;
    mvcc_set_txn_status(mvcc, transaction->xid, TXN_ACTIVE);
}

void beginTransactionWithIsolation(MVCC *mvcc, Transaction *transaction, int8_t isolation_level) {
    mvcc_flush_if_full(mvcc);
    transaction->xid = getTxnId(mvcc);
    transaction->isolation_level = isolation_level;
    mvcc_set_txn_status(mvcc, transaction->xid, TXN_ACTIVE);
}

void abortTransaction(MVCC *mvcc,Transaction *transaction) {
    mvcc_set_txn_status(mvcc, transaction->xid, TXN_ABORTED);
}

void commitTransaction(MVCC *mvcc, Transaction *transaction) {
    mvcc_set_txn_status(mvcc, transaction->xid, TXN_COMMITED);
}

int32_t getOldestXid(MVCC *mvcc){
    for (int i=0;i<MAX_TRANSACTIONS;i++) {
        if (mvcc->txn_status[i] == TXN_ACTIVE) {
            return mvcc->base_txn + i;
        }
    }
    return -1; // no transaction currently
}


#endif //QUAKEDB3_0_TRANSACTION_H