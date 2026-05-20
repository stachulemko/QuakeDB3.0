//
// Created by stas on 16.04.2026.
//

#ifndef QUAKEDB3_0_TRANSACTION_H
#define QUAKEDB3_0_TRANSACTION_H

#include "mvcc.h"

typedef struct {
    int32_t xid;
}Transaction;

void beginTransaction(MVCC *mvcc,Transaction *transaction) {
    transaction->xid = getTxnId(mvcc);
}

void commitTransaction(MVCC *mvcc, Transaction *transaction) {
    mvcc->txn_status[transaction->xid] = 1; // 1 = committed
}


#endif //QUAKEDB3_0_TRANSACTION_H
