//
// Created by stas on 2.09.2026.
//

#ifndef QUAKEDB3_0_HOTUPDATE_H
#define QUAKEDB3_0_HOTUPDATE_H
#include "sqlExecutor.h"
#include "../../indexes/indexes/btreeFileOperation.h"

/*
 * hotUpdate — wywolywane PRZED zapisem nowego tuple.
 *   Usuwa wartosci starego tuple z indeksow B-tree.
 */
static inline void hotUpdate(SqlExecutor *se, Tuple *oldTuple, int32_t oldBlockId,
                              FSMMapBtree *fsmMapBtree, BtreeBuffors *btreeBuffors,
                              int32_t tableId) {
    (void)se;
    btree_delete_tuple_indexes(fsmMapBtree, btreeBuffors, tableId, oldTuple, oldBlockId);
}

/*
 * hotUpdateA — wywolywane PO zapisie nowego tuple.
 *   Wstawia wartosci nowego tuple do indeksow B-tree.
 */
static inline void hotUpdateA(SqlExecutor *se, Tuple *newTuple, int32_t newBlockId,
                               FSMMapBtree *fsmMapBtree, BtreeBuffors *btreeBuffors,
                               int32_t tableId) {
    (void)se;
    btree_insert_tuple_indexes(fsmMapBtree, btreeBuffors, tableId, newTuple, newBlockId);
}

#endif //QUAKEDB3_0_HOTUPDATE_H
