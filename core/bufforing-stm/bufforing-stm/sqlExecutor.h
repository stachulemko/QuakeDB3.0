//
// Created by stas on 16.04.2026.
//

#ifndef QUAKEDB3_0_SQLEXECUTOR_H
#define QUAKEDB3_0_SQLEXECUTOR_H

#include "dataBuffor.h"
#include "block8kb.h"
#include "transaction.h"
#include "fsmMap.h"
#include "../../memory-mgmt/memory-mgmt/all_var.h"
#include "../../indexes/indexes/btreeFileOperation.h"
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * ResultTuple — wynik zapytania SELECT
 * ========================================================================= */

typedef struct {
    Tuple   *tuples[RESULT_SPACE];
    int32_t  tuple_count;
} ResultTuple;

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

    /* B-tree indexes (opcjonalne — NULL jesli brak indeksow) */
    FSMMapBtree  *fsmMapBtree;
    BtreeBuffors *btreeBuffors;
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

/* Bit w t_infomask: tuple wstawiony przez UPDATE — pomijany w skanowaniu, dostępny tylko przez chain traversal */
#define INFOMASK_CHAIN_MEMBER ((int32_t)0x0001)
#define INFOMASK_DEAD         ((int32_t)0x0002)

uint32_t pack(int16_t a, int16_t b) {
    return ((uint32_t)(uint16_t)a << 16) | (uint16_t)b;
}
void unpack(uint32_t p, int16_t *a, int16_t *b) {
    *a = (int16_t)(p >> 16);
    *b = (int16_t)(p & 0xFFFF);
}

/* xmax == 0 or xmax < 0 means "not deleted" */
static inline int8_t xmax_is_none(int32_t xmax) {
    return xmax == 0 || xmax < 0;
}

static inline int8_t isVisible(Tuple *tuple, int32_t xid) {
    if (tuple->header.t_xmin > xid) return 0;
    if (!xmax_is_none(tuple->header.t_xmax) && tuple->header.t_xmax <= xid) return 0;
    return 1;
}

static inline int8_t canVacuum(Tuple *tuple, MVCC *mvcc) {
    if (xmax_is_none(tuple->header.t_xmax)) return 0;
    int32_t oldest = getOldestXid(mvcc);
    /* oldest == -1 → brak aktywnych transakcji → można usunąć wszystko z xmax */
    if (oldest == -1) return 1;
    /* xmax < oldest → każda aktywna transakcja (xid >= oldest) widzi tuplę jako martwą */
    return tuple->header.t_xmax < oldest;
}

static inline void vacumingTupleIfDead(Tuple *currTuple, Tuple *prevTuple, MVCC *mvcc,DataBuffor *buf) {
    if (currTuple != NULL) {
        if (canVacuum(currTuple, mvcc)) {
            /* przepnij łańcuch: prevTuple przeskakuje przez currTuple */
            prevTuple->header.t_cid = currTuple->header.t_cid;
            currTuple->header.t_infomask |= INFOMASK_DEAD;
            buf->isDirty = 1;
            /* tuple jest częścią bloku — pamięć bloku recyklingowana osobno */
        }
    }
}

/* Follow chain for Repeatable Read: returns the version visible at xid */
static Tuple *sql_followChainRR(Tuple *start, Buffors *buffors, int32_t tableId, int32_t xid, MVCC *mvcc) {
    Tuple *prev = NULL;         
    Tuple *curr = start;
    while (curr != NULL) {
        if (curr->header.t_xmin <= xid &&
            (xmax_is_none(curr->header.t_xmax) || curr->header.t_xmax > xid)) {
            return curr;
        }
        if (curr->header.t_cid == 0) return NULL;
        int16_t blockId, tupleIdx;
        unpack((uint32_t)curr->header.t_cid, &blockId, &tupleIdx);
        DataBuffor *buf = getBuffor(tableId, (int32_t)blockId, buffors);
        if (buf == NULL) return NULL;
        if (buf->universalBlock == NULL) { buf->pinCount = 0; return NULL; }
        Tuple *next = &buf->universalBlock->block->tuples[(int32_t)tupleIdx];
        vacumingTupleIfDead(curr, prev != NULL ? prev : curr, mvcc,buf);
        prev = curr;
        curr = next;
        buf->pinCount = 0;
    }
    return NULL;
}

/* Follow chain for Read Committed: returns the latest version with xmin <= xid */
static Tuple *sql_followChainRC(Tuple *start, Buffors *buffors, int32_t tableId, int32_t xid, MVCC *mvcc) {
    Tuple *prev = NULL;
    Tuple *cur = start;
    Tuple *lastVisible = NULL;
    while (cur != NULL) {
        if (cur->header.t_xmin <= xid) {
            lastVisible = cur;
        }
        if (cur->header.t_cid == 0) break;
        int16_t blockId, tupleIdx;
        unpack((uint32_t)cur->header.t_cid, &blockId, &tupleIdx);
        DataBuffor *buf = getBuffor(tableId, (int32_t)blockId, buffors);
        if (buf == NULL) break;
        if (buf->universalBlock == NULL) { buf->pinCount = 0; break; }
        Tuple *next = &buf->universalBlock->block->tuples[(int32_t)tupleIdx];
        vacumingTupleIfDead(cur, prev != NULL ? prev : cur, mvcc, buf);
        prev = cur;
        cur = next;
        buf->pinCount = 0;
    }
    return lastVisible;
}


static void sql_doUpdate(SqlExecutor *se, Tuple *t,Buffors *buffors,FSMCache *c,FSMMapAll *fsmMapAll,MVCC *mvcc,int32_t blockId) {
    /* hotUpdate: usun stary tuple z indeksow przed oznaczeniem xmax */
    if (se->fsmMapBtree != NULL && se->btreeBuffors != NULL) {
        btree_delete_tuple_indexes(se->fsmMapBtree, se->btreeBuffors, se->tableId, t, blockId);
    }

    t->header.t_xmax = se->transaction->xid;

    Tuple newTuple = *t;

    newTuple.header.t_xmin = se->transaction->xid;

    for (int j = 0; j < se->updCount; j++) {
        newTuple.dnb.data[se->updColumns[j]] = se->updValues[j];
    }

    DataBuffor *dataBuffor = addTupleToOtherFunction(buffors, c, fsmMapAll, mvcc, se->tableId,
        newTuple.dnb.data, newTuple.dnb.data_count,
        newTuple.dnb.bit_map, newTuple.dnb.bit_map_count,
        se->transaction->xid, -1, -1, newTuple.header.t_infomask,
        newTuple.header.t_hoff, newTuple.header.null_bitmap, newTuple.header.optional_oid);

    int32_t newIdx     = dataBuffor->universalBlock->block->tuple_count - 1;
    int32_t newBlockId = (int32_t)dataBuffor->universalBlock->block->header.block_id;

    /* hotUpdateA: dodaj nowy tuple do indeksow po jego zapisaniu */
    if (se->fsmMapBtree != NULL && se->btreeBuffors != NULL) {
        btree_insert_tuple_indexes(se->fsmMapBtree, se->btreeBuffors, se->tableId,
                                   &dataBuffor->universalBlock->block->tuples[newIdx], newBlockId);
    }

    uint32_t pointerToNextUpdatedTuple = pack((int16_t)newBlockId, (int16_t)newIdx);
    t->header.t_cid = pointerToNextUpdatedTuple;
    /* oznacz nowy tuple jako chain member przez infomask — t_cid=0 oznacza koniec łańcucha */
    dataBuffor->universalBlock->block->tuples[newIdx].header.t_infomask |= INFOMASK_CHAIN_MEMBER;
    dataBuffor->universalBlock->block->tuples[newIdx].header.t_cid = 0;
    dataBuffor->isDirty = 1;
    dataBuffor->pinCount = 0;
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
 * 5. Single-pass przez jeden blok (tylko Repeatable Read)
 *    kolejność: izolacja → WHERE → UPDATE → SELECT/wynik
 * ========================================================================= */

void sql_execBlock(Block8kb *block, SqlExecutor *se, ResultTuple *result,
                   Buffors *buffors, FSMCache *c, FSMMapAll *fsmMapAll, MVCC *mvcc) {
    for (int i = 0; i < block->tuple_count; i++) {
        if (result->tuple_count >= RESULT_SPACE) break;

        Tuple *t = &block->tuples[i];

        /* pomiń chain members — dostępne tylko przez chain traversal z roota */
        if (t->header.t_infomask & INFOMASK_CHAIN_MEMBER) continue;

        /* RR: idź po łańcuchu i znajdź wersję widoczną przy xid tej transakcji */
        Tuple *visible = sql_followChainRR(t, buffors, se->tableId, se->transaction->xid, mvcc);
        if (visible == NULL) continue;

        if (se->where && !sql_matchWhere(se, visible)) continue;
        if (se->update) sql_doUpdate(se, visible, buffors, c, fsmMapAll, mvcc, block->header.block_id);

        if (se->select) {
            result->tuples[result->tuple_count++] = sql_doSelect(se, visible);
        } else {
            result->tuples[result->tuple_count++] = visible;
        }
    }
}


void sql_fullScan(SqlExecutor *se, ResultTuple *result, Buffors *buffors,
                  FSMCache *c, FSMMapAll *fsmMapAll, MVCC *mvcc) {
    if (VIEW_MODE == 2) {
        // Read Committed — chain traversal: pomiń chain members, od roota idź do końca łańcucha
        for (int i = 1; i <= se->endBlock; i++) {
            DataBuffor *buf = getBuffor(se->tableId, i, buffors);
            if (buf == NULL) continue;
            if (buf->universalBlock == NULL) { buf->pinCount = 0; continue; }
            Block8kb   *block = buf->universalBlock->block;
            for (int j = 0; j < block->tuple_count; j++) {
                if (result->tuple_count >= RESULT_SPACE) break;
                Tuple *t = &block->tuples[j];

                /* pomiń chain members */
                if (t->header.t_infomask & INFOMASK_CHAIN_MEMBER) continue;

                /* RC: idź po łańcuchu do najnowszej wersji z xmin <= xid */
                Tuple *visible = sql_followChainRC(t, buffors, se->tableId, se->transaction->xid, mvcc);
                if (visible == NULL) continue;

                if (se->where && !sql_matchWhere(se, visible)) continue;
                if (se->update) sql_doUpdate(se, visible, buffors, c, fsmMapAll, mvcc, i);
                if (se->select) {
                    result->tuples[result->tuple_count++] = sql_doSelect(se, visible);
                } else {
                    result->tuples[result->tuple_count++] = visible;
                }
            }
            buf->isUsed   = 1;
            buf->pinCount = 0;
        }
        return;
    }

    // Repeatable Read — skanuj wszystkie bloki
    for (int i = 1; i <= se->endBlock; i++) {
        DataBuffor *buf = getBuffor(se->tableId, i, buffors);
        if (buf == NULL) continue;
        if (buf->universalBlock == NULL) { buf->pinCount = 0; continue; }
        sql_execBlock(buf->universalBlock->block, se, result, buffors, c, fsmMapAll, mvcc);
        buf->isUsed   = 1;
        buf->pinCount = 0;
    }
}

#endif //QUAKEDB3_0_SQLEXECUTOR_H