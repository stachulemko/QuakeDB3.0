#ifndef universal_block_H
#define universal_block_H

#include <stdlib.h>
#include "block8kb.h"
#include "table_header.h"

typedef struct {
    Block8kb    *block;
    TableHeader *header;
} UniversalBlock;

static UniversalBlock *createUniversalBlock(uint8_t buf[BLOCK_SIZE]) {
    int16_t tag = 0;
    UniversalBlock *ub;
    unmarshal_int16(&tag, buf);
    ub = (UniversalBlock *)calloc(1, sizeof(UniversalBlock));
    switch (tag) {
    case ID_TABLE_HEADER:
        
        if (!ub) return NULL;
        ub->header = (TableHeader *)malloc(sizeof(TableHeader));
        if (!ub->header) { free(ub); return NULL; }
        table_header_unmarshal(ub->header, buf);
        return ub;
    case ID_ALL_BLOCK:
        if (!ub) return NULL;
        ub->block = (Block8kb *)malloc(sizeof(Block8kb));
        if (!ub->block) { free(ub); return NULL; }
        block8kb_unmarshal(ub->block, buf);
        return ub;
    default:
        free(ub);
        return NULL;
    }
    
}

static void marshalUniversalBlock(uint8_t buf[BLOCK_SIZE], const UniversalBlock *ub) {
    if (ub->header) {
        table_header_marshal(buf, ub->header);
    } else if (ub->block) {
        block8kb_marshal(buf, ub->block);
    }
}
int32_t getBlockId(const UniversalBlock *ub) {
    if (ub->header) {
        return ub->header->block_id;
    } else if (ub->block) {
        return ub->block->header.block_id;
    }
    return -1; /* nieznany typ */
}


#endif
