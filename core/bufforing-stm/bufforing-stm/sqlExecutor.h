//
// Created by stas on 16.04.2026.
//

#ifndef QUAKEDB3_0_SQLEXECUTOR_H
#define QUAKEDB3_0_SQLEXECUTOR_H

#include "dataBuffor.h"
#include "block8kb.h"
#include "transaction.h"
#include "fsmMap.h"
#include "queryExecutor.h"
#include "../../memory-mgmt/memory-mgmt/all_var.h"
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Operators (pasowane do evaluateAllVar)
 *   SQL_EQ  = 0   ( =  )
 *   SQL_NEQ = 1   ( != )
 *   SQL_LT  = 2   ( <  )
 *   SQL_GT  = 3   ( >  )
 *   SQL_LE  = 4   ( <= )
 *   SQL_GE  = 5   ( >= )
 * ========================================================================= */

#define SQL_EQ  0
#define SQL_NEQ 1
#define SQL_LT  2
#define SQL_GT  3
#define SQL_LE  4
#define SQL_GE  5

/* =========================================================================
 * Warunek WHERE: kolumna OP wartość
 * ========================================================================= */

typedef struct {
    int32_t column;   /* indeks kolumny w tuple */
    AllVar  value;    /* wartość do porównania  */
    int8_t  op;       /* SQL_EQ / SQL_NEQ / SQL_LT / SQL_GT / SQL_LE / SQL_GE */
    int16_t method;   /* metoda dla stringów: 0=len+lex, 1=lex */
} SqlCondition;

/* =========================================================================
 * SqlExecutor — opis zapytania
 * ========================================================================= */

typedef struct {
    /* SELECT */
    int8_t  select;
    int32_t selColumns[MAX_COLUMNS];
    int32_t selCount;

    /* WHERE (warunki AND) */
    int8_t        where;
    SqlCondition  conditions[MAX_COLUMNS];
    int32_t       condCount;

    /* UPDATE */
    int8_t  update;
    int32_t updColumns[MAX_COLUMNS];
    AllVar  updValues[MAX_COLUMNS];
    int32_t updCount;

    /* FROM */
    int32_t tableId;
    int32_t endBlock;

    Transaction *transaction;
} SqlExecutor;

/* =========================================================================
 * Builder helpers
 * ========================================================================= */

void sql_setSelect(SqlExecutor *se, int32_t columns[], int32_t count) {
    se->select = 1;
    se->selCount = count;
    for (int i = 0; i < count; i++) se->selColumns[i] = columns[i];
}

void sql_addWhere(SqlExecutor *se, int32_t column, AllVar value, int8_t op, int16_t method) {
    se->where = 1;
    int idx = se->condCount++;
    se->conditions[idx].column = column;
    se->conditions[idx].value  = value;
    se->conditions[idx].op     = op;
    se->conditions[idx].method = method;
}

void sql_setUpdate(SqlExecutor *se, int32_t columns[], AllVar values[], int32_t count) {
    se->update = 1;
    se->updCount = count;
    for (int i = 0; i < count; i++) {
        se->updColumns[i] = columns[i];
        se->updValues[i]  = values[i];
    }
}

void sql_setFrom(SqlExecutor *se, int32_t tableId, int32_t endBlock, Transaction *txn) {
    se->tableId  = tableId;
    se->endBlock = endBlock;
    se->transaction = txn;
}

/* =========================================================================
 * 1. WHERE — wszystkie warunki AND (używa evaluateAllVar)
 * ========================================================================= */

static int8_t sql_matchWhere(SqlExecutor *se, Tuple *t) {
    for (int j = 0; j < se->condCount; j++) {
        SqlCondition *c = &se->conditions[j];
        AllVar *v = &t->dnb.data[c->column];
        if (!evaluateAllVar(v, &c->value, c->op, c->method)) return 0;
    }
    return 1;
}

/* =========================================================================
 * 2. SELECT — projekcja kolumn do nowego tuple (malloc)
 * ========================================================================= */

static Tuple *sql_doSelect(SqlExecutor *se, Tuple *t) {
    Tuple *r = (Tuple *)malloc(sizeof(Tuple));
    tuple_init(r);
    r->dnb.data_count    = se->selCount;
    r->dnb.bit_map_count = se->selCount;
    for (int j = 0; j < se->selCount; j++) {
        r->dnb.data[j]    = t->dnb.data[se->selColumns[j]];
        r->dnb.bit_map[j] = t->dnb.bit_map[se->selColumns[j]];
    }
    return r;
}

/* =========================================================================
 * 3. UPDATE — modyfikacja in-place
 * ========================================================================= */

static void sql_doUpdate(SqlExecutor *se, Tuple *t) {
    for (int j = 0; j < se->updCount; j++) {
        t->dnb.data[se->updColumns[j]] = se->updValues[j];
    }
}

/* =========================================================================
 * 4. Izolacja transakcyjna
 *    VIEW_MODE 1 = Repeatable Read  — widoczne jeśli xmin <= xid i nie usunięte
 *    VIEW_MODE 2 = Read Committed   — śledzi najnowszy xid w bloku
 * ========================================================================= */

// Repeatable Read: zwraca 1 jeśli tuple jest widoczny
static int8_t sql_isVisibleRR(SqlExecutor *se, Tuple *t) {
    if (t->header.t_xmin > se->transaction->xid) return 0;
    if (t->header.t_xmax != 0 && t->header.t_xmax <= se->transaction->xid) return 0;
    return 1;
}

// Read Committed: aktualizuje śledzony najnowszy xid
static void sql_trackRC(Tuple *t, int32_t index, int32_t *xidMax, int32_t *bestIndex) {
    int32_t xid = (t->header.t_xmin > t->header.t_xmax)
                  ? t->header.t_xmin
                  : t->header.t_xmax;
    if (xid > *xidMax) {
        *xidMax    = xid;
        *bestIndex = index;
    }
}

/* =========================================================================
 * 5. Single-pass przez jeden blok
 *    kolejność: izolacja → WHERE → UPDATE → SELECT/wynik
 * ========================================================================= */

void sql_execBlock(Block8kb *block, SqlExecutor *se, ResultTuple *result) {
    if (VIEW_MODE == 2) {
        // Read Committed — znajdź najnowszą widoczną wersję w bloku
        int32_t xidMax    = 0;
        int32_t bestIndex = -1;

        for (int i = 0; i < block->tuple_count; i++) {
            sql_trackRC(&block->tuples[i], i, &xidMax, &bestIndex);
        }

        if (bestIndex < 0 || result->tuple_count >= RESULT_SPACE) return;

        Tuple *t = &block->tuples[bestIndex];
        if (se->where && !sql_matchWhere(se, t)) return;
        if (se->update) sql_doUpdate(se, t);
        if (se->select) {
            result->tuples[result->tuple_count++] = sql_doSelect(se, t);
        } else {
            result->tuples[result->tuple_count++] = t;
        }
        return;
    }

    // Repeatable Read — skanuj wszystkie widoczne tuple
    for (int i = 0; i < block->tuple_count; i++) {
        if (result->tuple_count >= RESULT_SPACE) break;

        Tuple *t = &block->tuples[i];

        if (!sql_isVisibleRR(se, t)) continue;
        if (se->where && !sql_matchWhere(se, t)) continue;

        if (se->update) sql_doUpdate(se, t);

        if (se->select) {
            result->tuples[result->tuple_count++] = sql_doSelect(se, t);
        } else {
            result->tuples[result->tuple_count++] = t;
        }
    }
}


void sql_fullScan(SqlExecutor *se, ResultTuple *result, Buffors *buffors) {
    for (int i = 1; i <= se->endBlock; i++) {
        DataBuffor *buf = getBuffor(se->tableId, i, buffors);
        sql_execBlock(buf->universalBlock->block, se, result);
        buf->isUsed   = 1;
        buf->pinCount = 0;
    }
}

#endif //QUAKEDB3_0_SQLEXECUTOR_H