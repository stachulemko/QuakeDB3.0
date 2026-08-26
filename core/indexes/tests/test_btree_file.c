#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "../../bufforing-stm/bufforing-stm/dataBuffor.h"
#include "../../memory-mgmt/memory-mgmt/all_var.h"
#include "../indexes/btreeFileOperation.h"

int main(void) {
    FSMMapBtree fsmMapBtree;
    BtreeBuffors btreeBuffors;

    fsm_btree_init(&fsmMapBtree);
    initBtreeBuffors(&btreeBuffors, 10);

    int32_t tableId = 99;
    int32_t columnIndex = 0;

    createBtree(&fsmMapBtree, tableId, columnIndex);

    // Verify blocks in FSM
    assert(fsm_btree_get_block(&fsmMapBtree, tableId, columnIndex, 0) != NULL);
    assert(fsm_btree_get_block(&fsmMapBtree, tableId, columnIndex, 1) != NULL);
    assert(fsm_btree_get_block(&fsmMapBtree, tableId, columnIndex, 2) != NULL);
    assert(fsm_btree_get_block(&fsmMapBtree, tableId, columnIndex, 3) != NULL);

    // Add unique key
    AllVar val1 = all_var_from_int32(10);
    addToBtree(val1, 101, &btreeBuffors, tableId, columnIndex, &fsmMapBtree);

    // Read back data
    dataBtree *data = getData(0, tableId, columnIndex, &btreeBuffors);
    assert(data != NULL);
    assert(data[0].data.type == ID_INT32);
    assert(data[0].data.val.i32 == 10);
    assert(data[0].pointerToBlocks != -1);

    // Verify first Blocks struct in block 3
    int32_t ptr1 = data[0].pointerToBlocks;
    int32_t blk1 = calculateBlock(ptr1);
    assert(blk1 == 3);
    BtreeBuffor *buf3 = getBtreeBuffor(tableId, columnIndex, 3, &btreeBuffors);
    assert(buf3 != NULL);
    int32_t bId1 = -1, next1 = -1;
    unmarshal_int32(&bId1, buf3->buf + (ptr1 % BLOCK_SIZE));
    unmarshal_int32(&next1, buf3->buf + (ptr1 % BLOCK_SIZE) + sizeof(int32_t));
    assert(bId1 == 101);
    assert(next1 == -1);
    free(data);

    // Add duplicate key (same val, different table blockId)
    addToBtree(val1, 102, &btreeBuffors, tableId, columnIndex, &fsmMapBtree);

    // Verify chain: ptr1 now points to next
    unmarshal_int32(&next1, buf3->buf + (ptr1 % BLOCK_SIZE) + sizeof(int32_t));
    assert(next1 != -1);
    int32_t bId2 = -1, next2 = -1;
    unmarshal_int32(&bId2, buf3->buf + (next1 % BLOCK_SIZE));
    unmarshal_int32(&next2, buf3->buf + (next1 % BLOCK_SIZE) + sizeof(int32_t));
    assert(bId2 == 102);
    assert(next2 == -1);

    // Add third duplicate key
    addToBtree(val1, 103, &btreeBuffors, tableId, columnIndex, &fsmMapBtree);

    // Verify chain: next1 now points to next2
    unmarshal_int32(&next2, buf3->buf + (next1 % BLOCK_SIZE) + sizeof(int32_t));
    assert(next2 != -1);
    int32_t bId3 = -1, next3 = -1;
    unmarshal_int32(&bId3, buf3->buf + (next2 % BLOCK_SIZE));
    unmarshal_int32(&next3, buf3->buf + (next2 % BLOCK_SIZE) + sizeof(int32_t));
    assert(bId3 == 103);
    assert(next3 == -1);

    printf("All btree file tests passed successfully!\n");
    return 0;
}
