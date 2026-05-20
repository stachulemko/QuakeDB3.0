//
// Created by stas on 16.04.2026.
//

#ifndef QUAKEDB3_0_QUERYEXECUTOR_H
#define QUAKEDB3_0_QUERYEXECUTOR_H

#include "block8kb.h"
#include "transaction.h"
#include "fsmMap.h"
#include <assert.h>
#include <string.h>

typedef struct {
    int8_t select;
    int32_t columns[MAX_COLUMNS];
    int32_t countColumns;

    int8_t insert;
    DataNullBitmap dnbInsert[MAX_COLUMNS];
    int32_t dnbInsertCount;

    int8_t where;
    int32_t columsWhere[MAX_COLUMNS];
    AllVar valuesWhere[MAX_COLUMNS];
    int32_t vCount;

    int8_t from;
    int32_t tableId;
    int32_t endBlock;

    int8_t end;

    Transaction *transaction;
}QueryExecutor;

typedef struct {
    Tuple tuples[RESULT_SPACE];
    int32_t tuple_count;
}ResultTuple;


void printResultTuple(ResultTuple *result) {
    for (int i = 0; i < result->tuple_count; i++) {
        Tuple t = result->tuples[i];
        printf("Tuple %d: ", i);
        for (int j = 0; j < t.dnb.data_count; j++) {
            AllVar value = t.dnb.data[j];
            if (value.type == ID_INT32) {
                printf("INT32: %d ", value.val.i32);
            } else if (value.type == ID_STRING) {
                printf("STRING: %s ", value.val.str);
            } else if (value.type == ID_INT64) {
                printf("INT64: %lld ", value.val.i64);
            }
        }
        printf("\n");
    }
}


void Qeselect(QueryExecutor *qe, int32_t columns[MAX_COLUMNS],int32_t countColumns) {
    qe->select = 1;
    qe->countColumns = countColumns;
    for (int i = 0; i < MAX_COLUMNS; i++) {
        qe->columns[i] = columns[i];
    }
}

void Qewhere(QueryExecutor *qe, int32_t columns[MAX_COLUMNS],AllVar valuesWhere[MAX_COLUMNS],int32_t vCount) {
    qe->where = 1;
    qe->vCount = vCount;
    for (int i = 0; i < MAX_COLUMNS; i++) {
        qe->columsWhere[i] = columns[i];
        qe->valuesWhere[i] = valuesWhere[i];
    }
}

void Qefrom(QueryExecutor *qe, int32_t tableId) {
    qe->from = 1;
    qe->tableId = tableId;
}

void Qeend(QueryExecutor *qe,Transaction **transaction,FSMCache *c) {
    qe->end = 1;
    qe->transaction = *transaction;
    qe->endBlock = fsm_cache_get(c,qe->tableId)->maxBlock;
}



void parseWhere(Block8kb * block, QueryExecutor *qe,ResultTuple *result) {
    for (int i=0;i<block->tuple_count;i++) {
        Tuple t = block->tuples[i];
        if (VIEW_MODE == 1) {
            if (t.header.t_xmin > qe->transaction->xid || (t.header.t_xmax != 0 && t.header.t_xmax <= qe->transaction->xid)) {
                continue;
            }
        }
        for (int j=0;j<qe->vCount;j++) {
            int colIndex = qe->columsWhere[j];
            AllVar value = t.dnb.data[colIndex];

            if (value.type == qe->valuesWhere[j].type) {

                if (value.type == ID_INT32 && value.val.i32 == qe->valuesWhere[j].val.i32) {
                    if (result->tuple_count < RESULT_SPACE) {
                        result->tuples[result->tuple_count++] = t;
                    }
                } else if (value.type == ID_STRING && strcmp(value.val.str, qe->valuesWhere[j].val.str) == 0) {
                    if (result->tuple_count < RESULT_SPACE) {
                        result->tuples[result->tuple_count++] = t;
                    }
                }
                else if (value.type == ID_INT64 && value.val.i64 == qe->valuesWhere[j].val.i64) {
                    if (result->tuple_count < RESULT_SPACE) {
                        result->tuples[result->tuple_count++] = t;
                    }
                }
            }
        }
    }
}

void parseSelect(Block8kb * block, QueryExecutor *qe,ResultTuple *result) {
    for (int i=0;i<block->tuple_count;i++) {
        if (result->tuple_count >= RESULT_SPACE) break;

        Tuple t = block->tuples[i];
        Tuple resultTuple = {0};
        tuple_init(&resultTuple);

        resultTuple.dnb.data_count = qe->countColumns;
        resultTuple.dnb.bit_map_count = qe->countColumns;

        for (int j=0;j<qe->countColumns;j++) {
            int colIndex = qe->columns[j];
            resultTuple.dnb.data[j] = t.dnb.data[colIndex];
            resultTuple.dnb.bit_map[j] = t.dnb.bit_map[colIndex];
        }

        result->tuples[result->tuple_count++] = resultTuple;
    }
}


void parser(QueryExecutor *qe,Block8kb *block,ResultTuple *result) {
    if (block == NULL) return;

    if (qe->from == 1) {
        if (qe->where == 1) {
            parseWhere(block,qe,result);
        }
        else if (qe->select == 1) {
            parseSelect(block,qe,result);
        }
    }
    else {
        assert(!"from is demanding !");
    }
}



#endif //QUAKEDB3_0_QUERYEXECUTOR_H
