//
// Created by stas on 16.04.2026.
//

#ifndef QUAKEDB3_0_TRANSACTION_H
#define QUAKEDB3_0_TRANSACTION_H

#include "mvcc.h"

#define TXN_ACTIVE 0
#define TXN_COMMITED 1
#define TXN_ABORTED 2


#define ISOLATION_REPEATABLE_READ 1
#define ISOLATION_READ_COMMITTED  2

typedef struct {
    int32_t xid;
    int8_t  isolation_level;
}Transaction;


void beginTransaction(MVCC *mvcc,Transaction *transaction) {
    transaction->xid = getTxnId(mvcc);
    transaction->isolation_level = VIEW_MODE;
    mvcc->txn_status[transaction->xid] = TXN_ACTIVE;
}

void beginTransactionWithIsolation(MVCC *mvcc, Transaction *transaction, int8_t isolation_level) {
    transaction->xid = getTxnId(mvcc);
    transaction->isolation_level = isolation_level;
    mvcc->txn_status[transaction->xid] = TXN_ACTIVE;
}

void abortTransaction(MVCC *mvcc,Transaction *transaction) {
    mvcc->txn_status[transaction->xid] = TXN_COMMITED;
}

void commitTransaction(MVCC *mvcc, Transaction *transaction) {
    mvcc->txn_status[transaction->xid] = TXN_ABORTED; // 1 = committed
}

int32_t getOldestXid(MVCC *mvcc){
    for (int i=0;i<MAX_TRANSACTIONS;i++) {
        if (mvcc->txn_status[i] == TXN_ACTIVE) {
            return i;
        }
    }
    return -1; // no transaction currently
}


#endif //QUAKEDB3_0_TRANSACTION_H
