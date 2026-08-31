//
// Created by stas on 16.04.2026.
//

#ifndef QUAKEDB3_0_QUERYEXECUTOR_H
#define QUAKEDB3_0_QUERYEXECUTOR_H

#include "dataBuffor.h"
#include "block8kb.h"
#include "transaction.h"
#include "fsmMap.h"
#include <assert.h>
#include <string.h>


typedef struct {
    int8_t update;
    AllVar valuesUpdate[MAX_COLUMNS];
    int32_t countColumnsUpdate;

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
    Tuple *tuples[RESULT_SPACE];
    int32_t tuple_count;
}ResultTuple;


void printResultTuple(ResultTuple *result) {
    for (int i = 0; i < result->tuple_count; i++) {
        Tuple *t = result->tuples[i];
        printf("Tuple %d: ", i);
        for (int j = 0; j < t->dnb.data_count; j++) {
            AllVar value = t->dnb.data[j];
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

void Qupdate(QueryExecutor *qe, AllVar valuesUpdate[MAX_COLUMNS],int32_t countColumnsUpdate) {
    qe->update = 1;
    qe->countColumnsUpdate = countColumnsUpdate;
    for (int i = 0; i < MAX_COLUMNS; i++) {
        qe->valuesUpdate[i] = valuesUpdate[i];
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
        Tuple *t = &block->tuples[i];
        if (VIEW_MODE == 1) {
            if (t->header.t_xmin > qe->transaction->xid || (t->header.t_xmax != 0 && t->header.t_xmax <= qe->transaction->xid)) {
                continue;
            }
        }
        for (int j=0;j<qe->vCount;j++) {
            int colIndex = qe->columsWhere[j];
            AllVar value = t->dnb.data[colIndex];

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

        Tuple *t = &block->tuples[i];
        Tuple *resultTuple = (Tuple *)malloc(sizeof(Tuple));
        tuple_init(resultTuple);

        resultTuple->dnb.data_count = qe->countColumns;
        resultTuple->dnb.bit_map_count = qe->countColumns;

        for (int j=0;j<qe->countColumns;j++) {
            int colIndex = qe->columns[j];
            resultTuple->dnb.data[j] = t->dnb.data[colIndex];
            resultTuple->dnb.bit_map[j] = t->dnb.bit_map[colIndex];
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

//-------------------------------

typedef struct {
    Block8kb*block;
    int32_t i;
}LastTupleData;

LastTupleData getNextTupleInCaseOfUpdate(Block8kb*block,QueryExecutor *qe,int32_t i,int32_t tableId,Buffors *buffors) {
    Tuple t = block->tuples[i];
    DataBuffor *data_buffor = NULL;
    Block8kb *currBlock = block;
    LastTupleData lastTupleData;
    lastTupleData.block = currBlock;
    lastTupleData.i = i;
    if (t.header.optional_oid!=-1) {
        int64_t prevIndex;
        (void)prevIndex;
        while (block->tuples[i].header.optional_oid!=0 ) {
            prevIndex = i;
            if (i+1>currBlock->tuple_count) {
                if (data_buffor != NULL) {
                    data_buffor->pinCount = 0;
                }
                data_buffor = getBuffor(tableId,currBlock->header.nextblock,buffors);
                data_buffor->pinCount = 1;
                currBlock = data_buffor->universalBlock->block;
            }
            i++;
        }

        lastTupleData.block = currBlock;
        lastTupleData.i = i;
        if (data_buffor != NULL) {
            data_buffor->pinCount = 0;
        }
        return lastTupleData;
    }
    if (data_buffor != NULL) {
        data_buffor->pinCount = 0;
    }
    return lastTupleData;
}


typedef struct {
    int32_t blockId;
    int32_t i;
}NextData;


void readCommited(Tuple t, QueryExecutor *query_executor, int32_t *blockTupleIndex, int32_t *xidMax, int32_t index) {
    if (query_executor == NULL) return;
    if (t.header.t_xmin > *xidMax || t.header.t_xmax > *xidMax) {
        if (t.header.t_xmin > t.header.t_xmax) {
            *xidMax = t.header.t_xmin;
            *blockTupleIndex = index;
        }
        else {
            *xidMax = t.header.t_xmax;
            *blockTupleIndex = index;
        }
    }
}



int8_t passIsolation(Tuple t, QueryExecutor *qe, int32_t index, int32_t *xidMax, int32_t *blockTupleIndex) {
    if (VIEW_MODE == 1) {
        if (t.header.t_xmin > qe->transaction->xid || (t.header.t_xmax != 0 && t.header.t_xmax <= qe->transaction->xid)) {
            return 1;
        }
        else {
            return 0;
        }
    }
    if (VIEW_MODE == 2) {
        readCommited(t, qe, blockTupleIndex, xidMax, index);
        return 1;
    }
    return 0;
}


void whereIf(Block8kb * block, QueryExecutor *qe,ResultTuple *result,Tuple *t) {
    (void)block;
    for (int j=0;j<qe->vCount;j++) {
        int colIndex = qe->columsWhere[j];
        AllVar value = t->dnb.data[colIndex];

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

void selectIf(Block8kb * block, QueryExecutor *qe,ResultTuple *result,Tuple *t) {
    (void)block;
    Tuple *resultTuple = (Tuple *)malloc(sizeof(Tuple));
    tuple_init(resultTuple);

    resultTuple->dnb.data_count = qe->countColumns;
    resultTuple->dnb.bit_map_count = qe->countColumns;

    for (int j=0;j<qe->countColumns;j++) {
        int colIndex = qe->columns[j];
        resultTuple->dnb.data[j] = t->dnb.data[colIndex];
        resultTuple->dnb.bit_map[j] = t->dnb.bit_map[colIndex];
    }

    result->tuples[result->tuple_count++] = resultTuple;
}


void applyCommand(Block8kb *block, QueryExecutor *qe, ResultTuple *result, Tuple *t, int16_t command) {
    if (command == 0) whereIf(block, qe, result, t);
    else if (command == 1) selectIf(block, qe, result, t);
}

NextData parserCommands(Block8kb * block, QueryExecutor *qe,ResultTuple *result,Buffors * buffors,int32_t tableId,int32_t blockId,int16_t command,int32_t starti) {
    DataBuffor* data_buffor=NULL;
    Block8kb *currBlock = block;
    int8_t oneTimeWrite = 1;
    int32_t i = starti;
    int32_t xidMax = 0;
    int32_t blockTupleIndex =0;
    while (1) {
        if (i >= currBlock->tuple_count) {
            if (VIEW_MODE == 2 && xidMax > 0) {
                applyCommand(currBlock, qe, result, &currBlock->tuples[blockTupleIndex], command);
            }
            NextData nextData;
            nextData.blockId = blockId;
            nextData.i = i;
            if (data_buffor != NULL) {
                data_buffor->pinCount = 0;
            }
            return nextData;
        }
        Tuple t = currBlock->tuples[i];
        if (passIsolation(t, qe, i, &xidMax, &blockTupleIndex) == 1 ) {
            i++;
            continue;
        }
        applyCommand(currBlock, qe, result, &currBlock->tuples[i], command);
        if (oneTimeWrite == 0 && t.header.optional_oid == 0) {
            oneTimeWrite = 1;
            if (currBlock != block) {
                NextData nextData;
                nextData.blockId = blockId;
                nextData.i = i;
                if (data_buffor != NULL) {
                    data_buffor->pinCount = 0;
                }
                return nextData;
            }
        }
        if (t.header.optional_oid!=-1) {
            if (oneTimeWrite == 1) {
                oneTimeWrite = 0;
            }
            if (i+1>currBlock->tuple_count) {
                if (data_buffor!=NULL) {
                    data_buffor->pinCount = 0;
                }

                data_buffor = getBuffor(tableId,currBlock->header.nextblock,buffors);
                data_buffor->pinCount = 1;
                currBlock = data_buffor->universalBlock->block;
                blockId++;
                i=0;
            }
        }
        i++;
    }
    NextData nextData;
    nextData.blockId = blockId;
    nextData.i = i;
    if (data_buffor != NULL) {
        data_buffor->pinCount = 0;
    }
    return nextData;
}




NextData parserWithUpdate(QueryExecutor *qe, Block8kb *block, ResultTuple *result, int32_t blockId, Buffors *buffors, int32_t tableId, int32_t i) {
    NextData nextData;
    nextData.blockId = blockId;
    nextData.i = -1;
    if (qe->from == 1) {
        if (qe->where == 1 && qe->select ==1 || qe->update == 1) {
            nextData = parserCommands(block, qe, result, buffors, tableId, blockId, 0, i);
        }
        if (qe->select == 1 && qe->update != 1) {
            nextData = parserCommands(block, qe, result, buffors, tableId, blockId, 1, i);
        }
        if (qe->update == 1) {
            nextData = parserCommands(block, qe, result, buffors, tableId, blockId, 0, i);
        }
    }
    else {
        assert(!"from is demanding !");
    }
    return nextData;
}



#endif //QUAKEDB3_0_QUERYEXECUTOR_H
