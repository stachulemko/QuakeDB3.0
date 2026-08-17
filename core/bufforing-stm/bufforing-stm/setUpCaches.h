//
// Created by stas on 18.05.2026.
//

#ifndef QUAKEDB3_0_SETUPCACHES_H
#define QUAKEDB3_0_SETUPCACHES_H

#include "dataBuffor.h"
#include "fsmMap.h"
#include "../../indexes/indexes/btreeFileOperation.h"

typedef struct {

    // ------------------------------------
    FSMCache *fsmCache = NULL;

    FSMMapAll *fsmMapAll = NULL;

    MVCC *mvcc = NULL ;

    Buffors *buffors = NULL ; // for block
    //--------------------------------------


    FSMCache *fsmCacheBtree = NULL;

    FSMMapAll *fsmMapAllBtree = NULL;

    FSMMapBtree

    BtreeBuffors *bTreeBuffor = NULL ; // for block




}DbEnv;

void createCaches(Cache *cache) {
    //===================================
    createBufforsM(&cache->buffors);

    initializeBuffors(cache->buffros, 3);
    //===================================
    FSMCacheCreateC(&cache->fsmCache);

    init_FSMMapAll(cache->fsmMapAll);
    //===================================
    create_MVCC(&cache->mvcc);
    //===================================


}



#endif //QUAKEDB3_0_SETUPCACHES_H
