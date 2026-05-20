//
// Created by stas on 16.04.2026.
//

#ifndef QUAKEDB3_0_FULLSCAN_H
#define QUAKEDB3_0_FULLSCAN_H

#include <stdint.h>
#include <stdlib.h>

#include "queryExecutor.h"
#include "dataBuffor.h"

typedef struct  {
    QueryExecutor *qe;
    ResultTuple *rt;
}FullScan ;

void fullScan(FullScan *fullscan,Buffors *buffors) {
    for (int i=1;i<=fullscan->qe->endBlock;i++) {
        DataBuffor* buffor = getBuffor(fullscan->qe->tableId,i,buffors);
        parser(fullscan->qe,buffor->universalBlock->block,fullscan->rt);
        buffor->isUsed = 1;
        buffor->pinCount = 0;
    }
}






#endif //QUAKEDB3_0_FULLSCAN_H
