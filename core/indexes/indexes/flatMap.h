//
// Created by stas on 28.08.2026.
//

#ifndef QUAKEDB3_0_FLATMAP_H
#define QUAKEDB3_0_FLATMAP_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Define a sentinel value that indicates an empty slot.
// This key cannot be used by actual data.
#define EMPTY_KEY -1
#define TABLE_SIZE 100000

static inline int64_t create_key(int16_t a, int32_t b) {
    // Cast 'a' to uint16_t first to prevent sign extension of negative values,
    // then cast to uint64_t before shifting. Cast 'b' to uint32_t for the same reason.
    return ((int64_t)(int16_t)a << 32) | (int32_t)b;
}


static inline void read_key(int64_t key, int16_t *a, int32_t *b) {
    *a = (int16_t)(key >> 32);        // Extract upper 32 bits and cast back to int16_t
    *b = (int32_t)(key & 0xFFFFFFFF); // Mask lower 32 bits and cast back to int32_t
}

// Our compressed 10-byte structure
struct __attribute__((__packed__)) Entry {
    int64_t key;
    int16_t value;
};

// 1. Fast hash generator for 64-bit integers (SplitMix64)
static inline uint64_t splitmix64(uint64_t key) {
    key += 0x9e3779b97f4a7c15;
    key = (key ^ (key >> 30)) * 0xbf58476d1ce4e5b9;
    key = (key ^ (key >> 27)) * 0x94d049bb133111eb;
    return key ^ (key >> 31);
}

// 2. Initialize memory with the sentinel value
static inline void initialize_table(struct Entry* table, size_t size) {
    for (size_t i = 0; i < size; i++) {
        table[i].key = EMPTY_KEY;
    }
}

// 3. Insertion or update (Linear Probing)
static inline int insert(struct Entry* table, size_t size, int64_t key, int16_t value) {
    if (key == EMPTY_KEY) return -1; // Error: attempt to use the sentinel key

    uint64_t hash = splitmix64((uint64_t)key);
    size_t index = hash % size;
    size_t start_index = index;

    // Look for an empty slot or the same key (for update)
    while (table[index].key != EMPTY_KEY && table[index].key != key) {
        index = (index + 1) % size; // Step 1 to the right in case of a collision

        if (index == start_index) {
            return -1; // Error: Table is 100% full
        }
    }

    table[index].key = key;
    table[index].value = value;
    return 0; // Success
}

// 4. Blazing fast search
static inline int16_t search(struct Entry* table, size_t size, int64_t key, int* found) {
    uint64_t hash = splitmix64((uint64_t)key);
    size_t index = hash % size;
    size_t start_index = index;

    // Iterate until we hit an empty slot
    while (table[index].key != EMPTY_KEY) {
        if (table[index].key == key) {
            *found = 1;
            return table[index].value; // Found
        }
        index = (index + 1) % size;

        if (index == start_index) {
            break; // We traversed the entire table
        }
    }

    *found = 0; // Return error flag if the key is not found
    return 0;
}

// 5. Find entry with smallest value >= minValue (best-fit scan).
//    Returns the table index of the best match, or -1 if none found.
static inline int32_t find_best_fit(struct Entry* table, size_t size, int16_t minValue) {
    int32_t bestIndex = -1;
    int16_t bestValue = INT16_MAX;
    for (size_t i = 0; i < size; i++) {
        if (table[i].key != EMPTY_KEY && table[i].value >= minValue && table[i].value < bestValue) {
            bestValue = table[i].value;
            bestIndex = (int32_t)i;
        }
    }
    return bestIndex;
}

// 6. Remove entry at given table index (direct clear)
static inline void remove_at_index(struct Entry* table, int32_t index) {
    table[index].key = EMPTY_KEY;
    table[index].value = 0;
}

// ======================== Buffered FlatMap ========================
// Hash table split into pages. Only N pages live in RAM (buffer pool).
// Rest is on disk. Pattern: getExisting → loadIfSpace → evict.
//
// ENTRIES_PER_PAGE = BLOCK_SIZE / sizeof(struct Entry) = 819
// totalPages = desired capacity / ENTRIES_PER_PAGE
// hash(key) % totalPages → pageId → slot within page

#include <string.h>
#include "../../memory-mgmt/memory-mgmt/file_manager_c.h"

#define ENTRIES_PER_PAGE (BLOCK_SIZE / (int)sizeof(struct Entry))

// Single buffer page in the pool
typedef struct {
    struct Entry table[ENTRIES_PER_PAGE];
    int32_t pageId;
    int32_t entryCount;
    int32_t pinCount;
    int8_t isUsed;
    int8_t isDirty;
} FlatMapBuffor;

// Buffer pool managing pages
typedef struct {
    FlatMapBuffor *buffors;     // pool array
    int32_t poolSize;           // how many pages fit in RAM
    int32_t totalPages;         // total logical pages (RAM + disk)
    char filePath[512];         // path to the disk file
} FlatMapBuffered;

// ----- internal helpers -----

static inline void flatmap_page_init(FlatMapBuffor *buf) {
    initialize_table(buf->table, ENTRIES_PER_PAGE);
    buf->pageId = -1;
    buf->entryCount = 0;
    buf->pinCount = 0;
    buf->isUsed = 0;
    buf->isDirty = 0;
}

static inline int32_t flatmap_count_entries(struct Entry *table) {
    int32_t count = 0;
    for (int i = 0; i < ENTRIES_PER_PAGE; i++) {
        if (table[i].key != EMPTY_KEY) count++;
    }
    return count;
}

// Save page to disk
static inline void flatmap_save_page(FlatMapBuffered *fm, FlatMapBuffor *buf) {
    FILE *f = fopen(fm->filePath, "r+b");
    if (f == NULL) {
        // file doesn't exist yet — create
        f = fopen(fm->filePath, "w+b");
        if (f == NULL) return;
    }
    fseek(f, (long)buf->pageId * BLOCK_SIZE, SEEK_SET);
    fwrite(buf->table, 1, BLOCK_SIZE, f);
    fclose(f);
}

// Load page from disk into buffer slot
static inline void flatmap_load_from_disk(FlatMapBuffered *fm, FlatMapBuffor *buf, int32_t pageId) {
    FILE *f = fopen(fm->filePath, "rb");
    if (f != NULL) {
        fseek(f, (long)pageId * BLOCK_SIZE, SEEK_SET);
        size_t read = fread(buf->table, 1, BLOCK_SIZE, f);
        fclose(f);
        if (read == BLOCK_SIZE) {
            buf->entryCount = flatmap_count_entries(buf->table);
        } else {
            // Page doesn't exist on disk yet — empty
            initialize_table(buf->table, ENTRIES_PER_PAGE);
            buf->entryCount = 0;
        }
    } else {
        // File doesn't exist yet — empty page
        initialize_table(buf->table, ENTRIES_PER_PAGE);
        buf->entryCount = 0;
    }
    buf->pageId = pageId;
    buf->isUsed = 1;
    buf->isDirty = 0;
    buf->pinCount = 1;
}

// ----- buffer pool management (same pattern as BtreeBuffors) -----

// 1. Check if page is already in pool
static inline FlatMapBuffor* flatmap_get_existing(FlatMapBuffered *fm, int32_t pageId) {
    for (int i = 0; i < fm->poolSize; i++) {
        if (fm->buffors[i].isUsed && fm->buffors[i].pageId == pageId) {
            fm->buffors[i].pinCount = 1;
            return &fm->buffors[i];
        }
    }
    return NULL;
}

// 2. Load into empty pool slot
static inline FlatMapBuffor* flatmap_load_if_space(FlatMapBuffered *fm, int32_t pageId) {
    for (int i = 0; i < fm->poolSize; i++) {
        if (!fm->buffors[i].isUsed) {
            flatmap_load_from_disk(fm, &fm->buffors[i], pageId);
            return &fm->buffors[i];
        }
    }
    return NULL;
}

// 3. Evict a page (dirty → save to disk), then load the new one
static inline FlatMapBuffor* flatmap_evict(FlatMapBuffered *fm, int32_t pageId) {
    while (1) {
        for (int i = 0; i < fm->poolSize; i++) {
            if (fm->buffors[i].isUsed && fm->buffors[i].pinCount == 0) {
                // Write back if dirty
                if (fm->buffors[i].isDirty) {
                    flatmap_save_page(fm, &fm->buffors[i]);
                }
                // Load the new page into this slot
                flatmap_load_from_disk(fm, &fm->buffors[i], pageId);
                return &fm->buffors[i];
            }
        }
    }
}

// Main function: get page from pool (getExisting → loadIfSpace → evict)
static inline FlatMapBuffor* flatmap_get_page(FlatMapBuffered *fm, int32_t pageId) {
    FlatMapBuffor *buf = flatmap_get_existing(fm, pageId);
    if (buf != NULL) return buf;

    buf = flatmap_load_if_space(fm, pageId);
    if (buf != NULL) return buf;

    return flatmap_evict(fm, pageId);
}

// ----- public API -----

// Initialize buffered flatMap
//   poolSize   — how many pages to keep in RAM (e.g. 4)
//   totalPages — total logical pages (e.g. 123 for ~100k entries)
//   filePath   — path to the disk file (e.g. "/path/to/flatmap_1.bin")
static inline int flatmap_init(FlatMapBuffered *fm, int32_t poolSize,
                               int32_t totalPages, const char *filePath) {
    if (fm == NULL || poolSize <= 0 || totalPages <= 0) return -1;
    fm->poolSize = poolSize;
    fm->totalPages = totalPages;
    snprintf(fm->filePath, sizeof(fm->filePath), "%s", filePath);
    fm->buffors = (FlatMapBuffor *)calloc(poolSize, sizeof(FlatMapBuffor));
    if (fm->buffors == NULL) return -1;
    for (int i = 0; i < poolSize; i++) {
        flatmap_page_init(&fm->buffors[i]);
    }
    // Create the file if it doesn't exist
    FILE *f = fopen(fm->filePath, "ab");
    if (f != NULL) fclose(f);
    return 0;
}

// Insert key-value into the buffered flatMap
static inline int flatmap_insert(FlatMapBuffered *fm, int64_t key, int16_t value) {
    if (fm == NULL || key == EMPTY_KEY) return -1;

    uint64_t hash = splitmix64((uint64_t)key);
    int32_t pageId = (int32_t)(hash % (uint64_t)fm->totalPages);

    FlatMapBuffor *page = flatmap_get_page(fm, pageId);
    if (page == NULL) return -1;

    // Check if key already exists (update vs new insert)
    int found = 0;
    search(page->table, ENTRIES_PER_PAGE, key, &found);

    int result = insert(page->table, ENTRIES_PER_PAGE, key, value);
    if (result == 0) {
        if (!found) {
            page->entryCount++;
        }
        page->isDirty = 1;
    }
    page->pinCount = 0;
    return result;
}

// Search for a key in the buffered flatMap
static inline int16_t flatmap_search(FlatMapBuffered *fm, int64_t key, int *found) {
    if (fm == NULL || key == EMPTY_KEY) {
        *found = 0;
        return 0;
    }

    uint64_t hash = splitmix64((uint64_t)key);
    int32_t pageId = (int32_t)(hash % (uint64_t)fm->totalPages);

    FlatMapBuffor *page = flatmap_get_page(fm, pageId);
    if (page == NULL) {
        *found = 0;
        return 0;
    }

    int16_t value = search(page->table, ENTRIES_PER_PAGE, key, found);
    page->pinCount = 0;
    return value;
}

// Flush all dirty pages to disk
static inline void flatmap_flush(FlatMapBuffered *fm) {
    if (fm == NULL) return;
    for (int i = 0; i < fm->poolSize; i++) {
        if (fm->buffors[i].isUsed && fm->buffors[i].isDirty) {
            flatmap_save_page(fm, &fm->buffors[i]);
            fm->buffors[i].isDirty = 0;
        }
    }
}

// Free all buffers and flush dirty pages
static inline void flatmap_free(FlatMapBuffered *fm) {
    if (fm == NULL) return;
    flatmap_flush(fm);
    free(fm->buffors);
    fm->buffors = NULL;
    fm->poolSize = 0;
}

#endif //QUAKEDB3_0_FLATMAP_H
