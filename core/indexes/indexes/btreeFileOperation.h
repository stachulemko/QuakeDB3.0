//
// Created by stas on 11.06.2026.
//

#ifndef QUAKEDB3_0_BTREEFILEOPERATION_H
#define QUAKEDB3_0_BTREEFILEOPERATION_H

#include <stdio.h>
#include "types_converter.h"
#include "../../memory-mgmt/memory-mgmt/file_manager_c.h"
#include "fsmMapBtree.h"
#include "log.h"
#include "../../memory-mgmt/memory-mgmt/all_var.h"


typedef struct {
    int32_t pointerToPointerWithData;
    int32_t pointerToNextLabel;
} nodeMetaData;

typedef struct {
    int32_t pointerDataToNextPointerData;
    int32_t pointerToData;
}pointerToData;

typedef struct {
    AllVar data;
    int32_t pointerToBlocks;
}dataBtree;

typedef struct {
    int32_t blockId;
    int32_t pointerToNextBlock;
}Blocks ;



static inline void btree_index_file_path(char *out, size_t outSize, int32_t tableId, int32_t columnIndex);

static inline int8_t btree_save_block_at_path(const char *path, const uint8_t buf[BLOCK_SIZE], int32_t blockNum) {
    FILE *f = fopen(path, "r+b");
    if (f == NULL) {
        LOG_ERROR("btree_save_block_at_path: cannot open %s\n", path);
        return 0;
    }

    if (fseek(f, (long)blockNum * BLOCK_SIZE, SEEK_SET) != 0) {
        fclose(f);
        LOG_ERROR("btree_save_block_at_path: cannot seek block %d in %s\n", blockNum, path);
        return 0;
    }

    size_t written = fwrite(buf, 1, BLOCK_SIZE, f);
    fclose(f);
    return (int8_t)(written == BLOCK_SIZE);
}



static inline void createBtreeFile(int32_t tableId , int32_t columnIndex) {
    char name[64];
    snprintf(name, sizeof(name), "%d.%d", tableId, columnIndex);
    createBinFile(INDEX_TABLE_PATH, name);
}

static inline void createBtree(FSMMapBtree *fsm,
int32_t tableId,int32_t columnIndex) {
    uint8_t buffer[BLOCK_SIZE*4];
    memset(buffer, 0, BLOCK_SIZE * 4);

    nodeMetaData metaData ={-1,-1};
    marshal_int32(buffer, metaData.pointerToPointerWithData); // -1
    marshal_int32(buffer + 4, metaData.pointerToNextLabel);   // -1
    show_bytes(buffer, 8);

    fsm_btree_create_index(fsm, tableId, columnIndex);

    fsm_btree_add_block(fsm, tableId, columnIndex, 0, 8);
    fsm_btree_add_block(fsm, tableId, columnIndex, 1, 0);
    fsm_btree_add_block(fsm, tableId, columnIndex, 2, 0);
    fsm_btree_add_block(fsm, tableId, columnIndex, 3, 0);

    createBtreeFile(tableId, columnIndex);

    char path[512];
    btree_index_file_path(path, sizeof(path), tableId, columnIndex);
    btree_save_block_at_path(path, buffer, 0);
    btree_save_block_at_path(path, buffer + BLOCK_SIZE, 1);
    btree_save_block_at_path(path, buffer + BLOCK_SIZE * 2, 2);
    btree_save_block_at_path(path, buffer + BLOCK_SIZE * 3, 3);
}


typedef struct{
    uint8_t buf[BLOCK_SIZE];
    int32_t tableId;
    int32_t columnIndex;
    int32_t blockId;
    int32_t pinCount;
    int8_t isUsed;
    int8_t isDirty;
}BtreeBuffor;

typedef struct {
    BtreeBuffor *buffors;
    int32_t count;
}BtreeBuffors;

typedef struct {
    int32_t dataOffsets[M];
    AllVar data[M];
    int32_t size;
}DataBtree;


static inline void createBtreeBufforM(BtreeBuffor **buf) {
    *buf = malloc(sizeof(BtreeBuffor));
}
static inline void createBtreeBufforC(BtreeBuffor **buf) {
    *buf = calloc(1,sizeof(BtreeBuffor));
}

static inline void initBtreeBuffors(BtreeBuffors *buffors, int32_t numberOfBuffors) {
    LOG_DEBUG("Initializing btree buffors...");
    buffors->buffors = (BtreeBuffor *)calloc(numberOfBuffors, sizeof(BtreeBuffor));
    if (buffors->buffors != NULL) {
        buffors->count = numberOfBuffors;
    } else {
        buffors->count = 0;
    }
}


static inline void btree_index_file_path(char *out, size_t outSize, int32_t tableId, int32_t columnIndex) {
    snprintf(out, outSize, "%s/%d.%d.bin", INDEX_TABLE_PATH, tableId, columnIndex);
}

static inline BtreeBuffor* getIfExistingBtree(int32_t tableId, int32_t columnIndex, int32_t blockId, BtreeBuffors *buffors) {
    for (int i = 0; i < buffors->count; i++) {
        if (buffors->buffors[i].isUsed == 1
            && buffors->buffors[i].tableId == tableId
            && buffors->buffors[i].columnIndex == columnIndex
            && buffors->buffors[i].blockId == blockId) {
            LOG_DEBUG("Btree block already exists in buffor.");
            buffors->buffors[i].pinCount = 1;
            return &buffors->buffors[i];
        }
    }
    LOG_DEBUG("Btree block does not exist in buffor.");
    return NULL;
}

static inline BtreeBuffor* loadIfSpaceBtree(int32_t tableId, int32_t columnIndex, int32_t blockId, BtreeBuffors *buffors) {
    for (int i = 0; i < buffors->count; i++) {
        if (buffors->buffors[i].isUsed == 0) {
            char path[512];
            btree_index_file_path(path, sizeof(path), tableId, columnIndex);
            uint8_t *diskBuf = fm_get_blockU(path, blockId);
            if (diskBuf == NULL) {
                return NULL;
            }

            LOG_DEBUG("Loading btree block into buffor...");
            memcpy(buffors->buffors[i].buf, diskBuf, BLOCK_SIZE);
            free(diskBuf);

            buffors->buffors[i].tableId = tableId;
            buffors->buffors[i].columnIndex = columnIndex;
            buffors->buffors[i].blockId = blockId;
            buffors->buffors[i].pinCount = 1;
            buffors->buffors[i].isUsed = 1;
            buffors->buffors[i].isDirty = 0;
            return &buffors->buffors[i];
        }
    }
    return NULL;
}

static inline BtreeBuffor* evictBtree(BtreeBuffors *buffors, int32_t tableId, int32_t columnIndex, int32_t blockId) {
    while (1) {
        for (int i = 0; i < buffors->count; i++) {
            if (buffors->buffors[i].isUsed == 1 && buffors->buffors[i].pinCount == 0) {
                BtreeBuffor *victim = &buffors->buffors[i];
                victim->pinCount = 1;
                LOG_DEBUG("Evicting btree block from buffor...");

                if (victim->isDirty == 1) {
                    char oldPath[512];
                    btree_index_file_path(oldPath, sizeof(oldPath), victim->tableId, victim->columnIndex);
                    if (!btree_save_block_at_path(oldPath, victim->buf, victim->blockId)) {
                        victim->pinCount = 0;
                        continue;
                    }
                }

                char newPath[512];
                btree_index_file_path(newPath, sizeof(newPath), tableId, columnIndex);
                uint8_t *diskBuf = fm_get_blockU(newPath, blockId);
                if (diskBuf == NULL) {
                    victim->pinCount = 0;
                    continue;
                }

                memcpy(victim->buf, diskBuf, BLOCK_SIZE);
                free(diskBuf);

                victim->tableId = tableId;
                victim->columnIndex = columnIndex;
                victim->blockId = blockId;
                victim->isDirty = 0;
                victim->isUsed = 1;
                return victim;
            }
        }
    }
}

static inline BtreeBuffor* getBtreeBuffor(int32_t tableId, int32_t columnIndex, int32_t blockId, BtreeBuffors *buffors) {
    BtreeBuffor *existingBuffor = getIfExistingBtree(tableId, columnIndex, blockId, buffors);
    if (existingBuffor != NULL) {
        return existingBuffor;
    }

    BtreeBuffor *loadedBuffor = loadIfSpaceBtree(tableId, columnIndex, blockId, buffors);
    if (loadedBuffor != NULL) {
        return loadedBuffor;
    }

    return evictBtree(buffors, tableId, columnIndex, blockId);
}

/*
  Drugi zakomentowany blok usunięty — był martwy i powielał nieużywane szkice logiki.
*/
//--------------------------------------------------------------

static inline int32_t calculateBlock(int32_t pointer) {
    return pointer / BLOCK_SIZE;
}

static inline DataBtree* getData(int32_t start, int32_t tableId, int32_t columnIndex,
                                     BtreeBuffors *btreeBuffors) {
        int32_t offset = start;
        int32_t block = calculateBlock(offset);
        BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
        if (buffor == NULL) return NULL;

        int32_t i = 0;
        // POPRAWKA 1: calloc(1, ...) - alokujemy 1 strukturę, a nie M struktur!
        DataBtree *data = (DataBtree *)calloc(1, sizeof(DataBtree));
        if (data == NULL) return NULL;

        int32_t pointerToPointerWithData = -1;
        int32_t pointerToNextPointerWithData = -1;
        int32_t pointerToData = -1;
        int16_t type = 0;
        int32_t length = 0;

        int32_t inBlockOffset = offset % BLOCK_SIZE;
        unmarshal_int32(&pointerToPointerWithData, buffor->buf + inBlockOffset);
        if (pointerToPointerWithData == -1) {
            data->size = 0;
            return data;
        }

        int32_t curPtr = pointerToPointerWithData;
        // (Ostrzeżenie: póki nie ma splitu, odczyta max M elementów z rosnącej w nieskończoność listy)
        while (curPtr != -1 && i < M) {
            block = calculateBlock(curPtr);
            buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
            if (buffor == NULL) break;

            int32_t curOffset = curPtr % BLOCK_SIZE;
            unmarshal_int32(&pointerToNextPointerWithData, buffor->buf + curOffset);
            unmarshal_int32(&pointerToData, buffor->buf + curOffset + sizeof(int32_t));

            if (pointerToData != -1) {
                int32_t dataBlock = calculateBlock(pointerToData);
                BtreeBuffor *dataBuffor = getBtreeBuffor(tableId, columnIndex, dataBlock, btreeBuffors);
                if (dataBuffor != NULL) {
                    int32_t dataOffset = pointerToData % BLOCK_SIZE;
                    unmarshal_int16(&type, dataBuffor->buf + dataOffset);
                    unmarshal_int32(&length, dataBuffor->buf + dataOffset + sizeof(int16_t));

                    if (type > 0 && length >= 0) {
                        all_var_unmarshal(&(data->data[i]), type, dataBuffor->buf + dataOffset + sizeof(int16_t) + sizeof(int32_t), length);

                        // POPRAWKA 2: Prawidłowe odczytywanie pointerToBlocks (z końca zapisu danych)
                        int32_t ptrToBlocks = -1;
                        unmarshal_int32(&ptrToBlocks, dataBuffor->buf + dataOffset + sizeof(int16_t) + sizeof(int32_t) + length);

                        data->dataOffsets[i] = ptrToBlocks;
                        i++;
                    }
                }
            }

            curPtr = pointerToNextPointerWithData;
        }

        // POPRAWKA 3: Zapisujemy ilość faktycznie zdekodowanych elementów
        data->size = i;
        return data;
    }

static inline int32_t findSmaller(AllVar *data, int32_t number, AllVar val) {
    for (int i = 0; i < number; i++) {
        if (all_var_cmp(&data[i], &val) > 0) {
            return i;
        }
    }
    return number;
}


static inline int32_t allocateBlocksEntry(int32_t blockIdVal, BtreeBuffors *btreeBuffors,
                                         int32_t tableId, int32_t columnIndex, FSMMapBtree *fsmMap) {
    BtreeBlockEntry *block3Entry = fsm_btree_get_block(fsmMap, tableId, columnIndex, 3);
    if (block3Entry == NULL) return -1;

    int32_t newBlocksOffset = 3 * BLOCK_SIZE + block3Entry->blockSize;
    int32_t blockId = calculateBlock(newBlocksOffset);
    BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, blockId, btreeBuffors);
    if (buffor == NULL) return -1;

    int32_t inBlockOffset = newBlocksOffset % BLOCK_SIZE;
    marshal_int32(buffor->buf + inBlockOffset, blockIdVal);
    marshal_int32(buffor->buf + inBlockOffset + sizeof(int32_t), -1);
    buffor->isDirty = 1;

    fsm_btree_append_to_block(fsmMap, tableId, columnIndex, 3, sizeof(int32_t) * 2);
    return newBlocksOffset;
}

int32_t getLastBlockOffset(int32_t firstBlockOffset, int32_t tableId, int32_t columnIndex, BtreeBuffors *btreeBuffors) {
    if (firstBlockOffset == -1 || btreeBuffors == NULL) {
        return -1;
    }

    int32_t curOffset = firstBlockOffset;
    int32_t nextOffset = -1;

    while (curOffset != -1) {
        int32_t block = calculateBlock(curOffset);
        BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
        if (buffor == NULL) break;

        int32_t inBlockOffset = curOffset % BLOCK_SIZE;

        // 3. Odczyt wskaźnika do NASTĘPNEGO bloku (+4 bajty, bo pierwsze 4B to blockId)
        unmarshal_int32(&nextOffset, buffor->buf + inBlockOffset + sizeof(int32_t));

        // 4. Jeśli następny to -1, to curOffset jest OSTATNIM blokiem!
        if (nextOffset == -1) {
            return curOffset; // Zwracamy offset ostatniego bloku
        }

        // 5. Przechodzimy do następnego bloku
        curOffset = nextOffset;
    }

    return curOffset;
}


void addBlockToVal(int32_t lastBlockOffset,int32_t tableId,int32_t columnIndex,BtreeBuffors *btreeBuffors,FSMMapBtree *fsmMap,int32_t blockIdVal) {
    int32_t blockOffset = getLastBlockOffset(lastBlockOffset, tableId, columnIndex, btreeBuffors);
    int32_t blockId = calculateBlock(blockOffset);
    int32_t blockExeededOffset = blockOffset % BLOCK_SIZE;
    BtreeBuffor * buffor = getBtreeBuffor(tableId, columnIndex, blockId, btreeBuffors);
    if (blockOffset != -1) {
        int32_t freeSpace = fsm_btree_get_free_space(fsmMap,tableId,columnIndex,blockExeededOffset);
        fsm_btree_update_block_size(fsmMap,tableId,columnIndex,blockId,freeSpace+sizeof(int32_t));
        marshal_int32(buffor->buf + blockExeededOffset, freeSpace);
        marshal_int32(buffor->buf + freeSpace ,blockId);
    }

}

int8_t checkForEqualAndAdd(AllVar val, int32_t blockIdVal, BtreeBuffors *btreeBuffors, int32_t tableId,
                              int32_t columnIndex, FSMMapBtree *fsmMap,DataBtree *data_btree) {
    DataBtree *dataAllvar = data_btree;
    for (int32_t i = 0; i < dataAllvar->size; i++) {
        if (all_var_cmp(&dataAllvar->data[i], &val) == 0) {
            int32_t indexFinded = dataAllvar->dataOffsets[i];
            addBlockToVal(indexFinded, tableId, columnIndex, btreeBuffors, fsmMap, blockIdVal);
            return 1; // Zwracamy 1, jeśli wartość została dodana
        }
    }
    return 0; // Zwracamy 0, jeśli wartość nie została dodana
}

static inline void addValDirectly(int32_t offset, AllVar val, int32_t pointerToBlocks, BtreeBuffors *btreeBuffors,
                                  int32_t tableId, int32_t columnIndex) {
    if (btreeBuffors == NULL) return;

    int32_t blockId = calculateBlock(offset);
    BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, blockId, btreeBuffors);
    if (buffor == NULL) return;

    int32_t blockOffset = offset % BLOCK_SIZE;
    marshal_int16(buffor->buf + blockOffset, val.type);
    marshal_int32(buffor->buf + blockOffset + sizeof(int16_t), all_var_size(&val));
    all_var_marshal(buffor->buf + blockOffset + sizeof(int16_t) + sizeof(int32_t), &val);
    marshal_int32(buffor->buf + blockOffset + sizeof(int16_t) + sizeof(int32_t) + all_var_size(&val), pointerToBlocks);
    buffor->isDirty = 1;
}

static inline int32_t getToLastPointerToData(int32_t pointerToDataStart, int32_t tableId,
                                                 int32_t columnIndex, BtreeBuffors *btreeBuffors) {
    // Zabezpieczenie przed błędnymi danymi wejściowymi
    if (pointerToDataStart == -1 || btreeBuffors == NULL) {
        return -1;
    }

    int32_t curPtdOffset = pointerToDataStart;
    int32_t lastPtdOffset = curPtdOffset;

    // Przechodzimy po łańcuchu dopóki nie trafimy na koniec (-1)
    while (curPtdOffset != -1) {
        // 1. Wyliczamy blok i pobieramy bufor
        int32_t block = calculateBlock(curPtdOffset);
        BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
        if (buffor == NULL) {
            break; // W razie błędu odczytu przerywamy
        }

        // 2. Odczytujemy pierwsze 4 bajty w strukturze (pointerDataToNextPointerData)
        int32_t inBlockOffset = curPtdOffset % BLOCK_SIZE;
        int32_t nextPtdOffset = -1;
        unmarshal_int32(&nextPtdOffset, buffor->buf + inBlockOffset);

        // 3. Jeśli następny offset to -1, to znaczy że jesteśmy w ostatnim elemencie
        if (nextPtdOffset == -1) {
            lastPtdOffset = curPtdOffset;
            break;
        }

        // 4. Przechodzimy do następnego elementu
        curPtdOffset = nextPtdOffset;
        lastPtdOffset = curPtdOffset;
    }

    // Zwracamy offset ostatniego pointerToData
    return lastPtdOffset;
}

//--------------------- new one -------------------------------- //
static inline int32_t findFreeSpace(int32_t tableId, int32_t columnIndex, int32_t sizeNeeded,
                                        BtreeBuffors *btreeBuffors, int32_t blockEntry, FSMMapBtree *fsmMapBtree) {

    // Zaczynamy szukanie od początkowego bloku dla danego typu (np. 1, 2, lub 3)
    int32_t curBlock = blockEntry;

    while (1) {
        // Sprawdzamy czy dany blok jest już zarejestrowany w mapie FSM
        BtreeBlockEntry *blockMeta = fsm_btree_get_block(fsmMapBtree, tableId, columnIndex, curBlock);

        if (blockMeta == NULL) {
            // Blok nie istnieje w FSM - dotarliśmy do końca przydzielonych bloków tego typu.
            // Rejestrujemy nowy blok z zajętym rozmiarem = 0.
            fsm_btree_add_block(fsmMapBtree, tableId, columnIndex, curBlock, 0);

            // Zwracamy fizyczny offset w pliku (początek nowo alokowanego bloku)
            return curBlock * BLOCK_SIZE;
        }
        else {
            // Blok istnieje. Sprawdzamy, czy po dodaniu 'sizeNeeded' nie przekroczymy limitu (btreeFreeSpace).
            if (blockMeta->blockSize + sizeNeeded <= btreeFreeSpace) {
                // Jest wystarczająco wolnego miejsca! Zwracamy dokładny offset, od którego można pisać.
                return (curBlock * BLOCK_SIZE) + blockMeta->blockSize;
            }
        }

        // W tym bloku nie ma już miejsca - skaczemy o 4 bloki dalej
        curBlock += 4;
    }
}

static inline int32_t createBlocksEntry(int32_t blockIdVal, int32_t tableId, int32_t columnIndex,
                                            BtreeBuffors *btreeBuffors, FSMMapBtree *fsmMap) {
        int32_t size = sizeof(int32_t) * 2; // 8 bajtów (blockId + nextBlockOffset)
        int32_t offset = findFreeSpace(tableId, columnIndex, size, btreeBuffors, 3, fsmMap);

        int32_t block = calculateBlock(offset);
        BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
        if (buffor != NULL) {
            int32_t inBlock = offset % BLOCK_SIZE;
            marshal_int32(buffor->buf + inBlock, blockIdVal);
            marshal_int32(buffor->buf + inBlock + sizeof(int32_t), -1); // na start brak następnego
            buffor->isDirty = 1;
            fsm_btree_append_to_block(fsmMap, tableId, columnIndex, block, size);
        }
        return offset;
    }

    // 2. Tworzy wpis dla dataBtree (Blok 2 - wartości klucza i wskaźnik do Blocks)
    static inline int32_t createDataEntry(AllVar val, int32_t pointerToBlocks, int32_t tableId,
                                          int32_t columnIndex, BtreeBuffors *btreeBuffors, FSMMapBtree *fsmMap) {
        int32_t dataSize = sizeof(int16_t) + sizeof(int32_t) + all_var_size(&val) + sizeof(int32_t);
        int32_t offset = findFreeSpace(tableId, columnIndex, dataSize, btreeBuffors, 2, fsmMap);

        int32_t block = calculateBlock(offset);
        BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
        if (buffor != NULL) {
            int32_t inBlock = offset % BLOCK_SIZE;
            marshal_int16(buffor->buf + inBlock, val.type);
            marshal_int32(buffor->buf + inBlock + sizeof(int16_t), all_var_size(&val));
            all_var_marshal(buffor->buf + inBlock + sizeof(int16_t) + sizeof(int32_t), &val);
            marshal_int32(buffor->buf + inBlock + sizeof(int16_t) + sizeof(int32_t) + all_var_size(&val), pointerToBlocks);
            buffor->isDirty = 1;
            fsm_btree_append_to_block(fsmMap, tableId, columnIndex, block, dataSize);
        }
        return offset;
    }

    // 3. Główna funkcja orkiestrująca, tworzy pointerToData (Blok 1) wywołując funkcje "w głąb"
    static inline void insertNewDataToLeaf(AllVar val, int32_t blockIdVal, int32_t lastPtdOffset, int32_t nodeOffset,
                                           int32_t tableId, int32_t columnIndex, BtreeBuffors *btreeBuffors, FSMMapBtree *fsmMap) {

        // A) Tworzymy od najgłębszej warstwy: wpis bloków (Block 3)
        int32_t blocksOffset = createBlocksEntry(blockIdVal, tableId, columnIndex, btreeBuffors, fsmMap);

        // B) Tworzymy wartość (Block 2) powiązaną ze stworzonym wpisem bloków
        int32_t dataOffset = createDataEntry(val, blocksOffset, tableId, columnIndex, btreeBuffors, fsmMap);

        // C) Tworzymy pointerToData (Block 1) i wiążemy go z listą w liściu
        int32_t size = sizeof(int32_t) * 2; // 8 bajtów (nextPtd + dataPtr)
        int32_t newPtdOffset = findFreeSpace(tableId, columnIndex, size, btreeBuffors, 1, fsmMap);

        int32_t block = calculateBlock(newPtdOffset);
        BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
        if (buffor != NULL) {
            int32_t inBlock = newPtdOffset % BLOCK_SIZE;
            marshal_int32(buffor->buf + inBlock, -1); // Next -> -1
            marshal_int32(buffor->buf + inBlock + sizeof(int32_t), dataOffset);
            buffor->isDirty = 1;
            fsm_btree_append_to_block(fsmMap, tableId, columnIndex, block, size);
        }

        // D) Podpinamy nowy element do "last" (poprzedniego na liście) lub do głowy węzła
        if (lastPtdOffset != -1) {
            int32_t prevBlock = calculateBlock(lastPtdOffset);
            BtreeBuffor *prevBuf = getBtreeBuffor(tableId, columnIndex, prevBlock, btreeBuffors);
            if (prevBuf != NULL) {
                marshal_int32(prevBuf->buf + (lastPtdOffset % BLOCK_SIZE), newPtdOffset);
                prevBuf->isDirty = 1;
            }
        } else {
            // Jeśli węzeł był zupełnie pusty, podpinamy do nagłówka (nodeOffset)
            int32_t nodeBlock = calculateBlock(nodeOffset);
            BtreeBuffor *nodeBuf = getBtreeBuffor(tableId, columnIndex, nodeBlock, btreeBuffors);
            if (nodeBuf != NULL) {
                marshal_int32(nodeBuf->buf + (nodeOffset % BLOCK_SIZE), newPtdOffset);
                nodeBuf->isDirty = 1;
            }
        }
    }

//--------------------- new one -------------------------------- //


static inline void addToBtree(AllVar val, int32_t blockIdVal, BtreeBuffors *btreeBuffors, int32_t tableId,
                              int32_t columnIndex, FSMMapBtree *fsmMap) {
        printf("DODAJĘ DO DRZEWA WARTOSC (Type: %d, Val: %d)\n", val.type, val.val.i32);
        if (btreeBuffors == NULL || fsmMap == NULL) {
            LOG_ERROR("addToBtree: Buffors or FSMMap is NULL");
            return;
        }

        BtreeTableEntry *table = fsm_btree_get_table(fsmMap, tableId);
        if (table == NULL) return;

        BtreeColumnIndex *col = fsm_btree_get_column(table, columnIndex);
        if (col == NULL) return;

        int32_t offset = 0;
        int32_t nextLevel = 0;

        while (offset != -1) {
            int32_t block = calculateBlock(offset);
            BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
            if (buffor == NULL) break;

            int32_t nextLevelPtr = -1;
            unmarshal_int32(&nextLevelPtr, buffor->buf + (offset % BLOCK_SIZE) + sizeof(int32_t));
            if (nextLevelPtr == -1) {
                break;
            }

            DataBtree *dataAllvar = getData(offset, tableId, columnIndex, btreeBuffors);
            if (dataAllvar == NULL) break;

            if (checkForEqualAndAdd(val, blockIdVal, btreeBuffors, tableId, columnIndex, fsmMap, dataAllvar)) {
                free(dataAllvar);
                return;
            }

            int32_t idx = findSmaller(dataAllvar->data, dataAllvar->size, val);

            offset = nextLevelPtr + (int32_t)(idx * sizeof(int32_t));
            free(dataAllvar);
        }
        int32_t nodeBlock = calculateBlock(offset);
        BtreeBuffor *nodeBuffor = getBtreeBuffor(tableId, columnIndex, nodeBlock, btreeBuffors);
        int32_t pointerToPointerWithData = -1;

        if (nodeBuffor != NULL) {
            unmarshal_int32(&pointerToPointerWithData, nodeBuffor->buf + (offset % BLOCK_SIZE));
        }

        int32_t last = getToLastPointerToData(pointerToPointerWithData, tableId, columnIndex, btreeBuffors);

        insertNewDataToLeaf(val, blockIdVal, last, offset, tableId, columnIndex, btreeBuffors, fsmMap);
}

static inline void addExistingValues(FSMMapBtree *fsm,
                                     int32_t tableId,
                                     int32_t columnIndex,
                                     BtreeBuffors *btreeBuffors,
                                     Buffors *buffors,
                                     FSMCache *fsmCache) {
    if (fsm == NULL || btreeBuffors == NULL || buffors == NULL || fsmCache == NULL) {
        LOG_ERROR("addExistingValues: invalid args");
        return;
    }

    BlockCounterEntry *counter = fsm_cache_get(fsmCache, tableId);
    if (counter == NULL) {
        LOG_ERROR("addExistingValues: table %d not found in fsmCache", tableId);
        return;
    }

    for (int32_t blockId = 0; blockId <= counter->maxBlock; ++blockId) {
        DataBuffor *dataBuffor = getBuffor(tableId, blockId, buffors);
        if (dataBuffor == NULL ||
            dataBuffor->universalBlock == NULL ||
            dataBuffor->universalBlock->block == NULL) {
            continue;
            }

        Block8kb *block = dataBuffor->universalBlock->block;

        for (int32_t t = 0; t < block->tuple_count; ++t) {
            Tuple *tuple = &block->tuples[t];
            if (columnIndex < 0 || columnIndex >= tuple->dnb.data_count) {
                continue;
            }

            addToBtree(tuple->dnb.data[columnIndex], blockId, btreeBuffors, tableId, columnIndex, fsm);
        }

        dataBuffor->pinCount = 0;
    }
}


static inline int32_t getBlockBtree(AllVar val, BtreeBuffors *btreeBuffors, int32_t tableId,
                                        int32_t columnIndex, FSMMapBtree *fsmMap, int8_t operator, int16_t method) {
        if (btreeBuffors == NULL || fsmMap == NULL) {
            LOG_ERROR("getBlockBtree: Buffors or FSMMap is NULL");
            return -1;
        }

        BtreeTableEntry *table = fsm_btree_get_table(fsmMap, tableId);
        if (table == NULL) return -1;

        BtreeColumnIndex *col = fsm_btree_get_column(table, columnIndex);
        if (col == NULL) return -1;

        int32_t offset = 0;

        // 1. Przechodzenie w dół drzewa (węzły wewnętrzne)
        while (offset != -1) {
            int32_t block = calculateBlock(offset);
            BtreeBuffor *buffor = getBtreeBuffor(tableId, columnIndex, block, btreeBuffors);
            if (buffor == NULL) break;

            int32_t nextLevelPtr = -1;
            unmarshal_int32(&nextLevelPtr, buffor->buf + (offset % BLOCK_SIZE) + sizeof(int32_t));
            if (nextLevelPtr == -1) {
                break;
            }

            DataBtree *dataAllvar = getData(offset, tableId, columnIndex, btreeBuffors);
            if (dataAllvar == NULL) break;

            int32_t validCount = 0;
            for (int i = 0; i < M; i++) {
                if (dataAllvar->data[i].type > 0) {
                    validCount++;
                }
            }

            int32_t idx = findSmaller(dataAllvar->data, validCount, val);
            offset = nextLevelPtr + (int32_t)(idx * sizeof(int32_t));
            free(dataAllvar);
        }

        DataBtree *dataAllvar = getData(offset, tableId, columnIndex, btreeBuffors);
        if (dataAllvar == NULL) return -1;

        for (int i = 0; i < M; i++) {
            if (dataAllvar->data[i].type > 0) {
                if (evaluateAllVar(&dataAllvar->data[i], &val, operator, method) == 1) {
                    int32_t ptrToBlocks = dataAllvar->dataOffsets[i];
                    free(dataAllvar);

                    if (ptrToBlocks != -1) {
                        int32_t curBlk = calculateBlock(ptrToBlocks);
                        BtreeBuffor *curBuffor = getBtreeBuffor(tableId, columnIndex, curBlk, btreeBuffors);
                        if (curBuffor != NULL) {
                            int32_t bId = -1;
                            unmarshal_int32(&bId, curBuffor->buf + (ptrToBlocks % BLOCK_SIZE));
                            return bId;
                        }
                    }
                    return -1;
                }
            }
        }

        free(dataAllvar);
        return -1;
    }


//------------------------------------------delete-----------------------------

void delte
#endif //QUAKEDB3_0_BTREEFILEOPERATION_H
