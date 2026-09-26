/* bq.h - bucket queue: key int32_t (block number), value int32_t.
 *
 * Configurable number of buckets (nbuckets), set in bq_init.
 * All operations are O(1) in the worst case (except rehashing the map).
 *
 *   bq_insert / bq_remove / bq_get / bq_incr / bq_decr / bq_min / bq_max
 *
 * Internal structure:
 *   head[nbuckets]  - one bucket per possible value
 *   arena           - nodes in an intrusive doubly linked list (indices, not pointers)
 *   3-level bitmap (dynamic)  - fast lookup of a non-empty bucket
 *   hash map        - key int32_t -> node index in the arena
 *
 * BqManager - keeps a separate bq_t per table with a configurable buffer size.
 */
#ifndef BQ_H
#define BQ_H

#include <stdint.h>
#include <stddef.h>

#define BQ_NIL      0xFFFFFFFFu

/* error codes */
#define BQ_OK        0
#define BQ_ENOTFOUND (-1)  /* no such key */
#define BQ_EEXISTS   (-2)  /* key already exists */
#define BQ_ERANGE    (-3)  /* value out of range [0, nbuckets-1] */
#define BQ_ENOMEM    (-4)

typedef struct {
    /* --- configuration --- */
    uint32_t  nbuckets;   /* number of buckets (max value + 1)    */
    uint32_t  max_entries;/* maximum buffer size (0 = no limit) */

    /* --- node arena --- */
    int32_t  *key;        /* block number                          */
    int32_t  *val;        /* current value (number of dead tuples) */
    uint32_t *next;       /* next node in the bucket (or free)     */
    uint32_t *prev;       /* previous node in the bucket           */
    uint32_t  cap;        /* arena capacity                        */
    uint32_t  used;       /* number of used nodes                  */
    uint32_t  free_head;

    /* --- buckets --- */
    uint32_t *head;       /* [nbuckets] head of the element list  */

    /* --- bitmap of non-empty buckets (dynamic) --- */
    uint64_t *l0;         /* 1 bit per bucket         (l0_words)   */
    uint64_t *l1;         /* 1 bit per l0 word        (l1_words)   */
    uint64_t  l2;         /* 1 bit per l1 word        (max 64)     */
    uint32_t  l0_words;   /* ceil(nbuckets / 64)                   */
    uint32_t  l1_words;   /* ceil(l0_words / 64)                   */

    /* --- hash map: key -> node index --- */
    uint32_t *slot;       /* BQ_NIL = empty, BQ_TOMB = tombstone   */
    uint32_t  smask;      /* size-1, size is a power of two       */
    uint32_t  scount;     /* used + tombstones                     */
} bq_t;

/* nbuckets = number of buckets (value range 0..nbuckets-1)
 * init_cap = initial arena capacity
 * max_entries = max number of entries (0 = no limit) */
int      bq_init(bq_t *q, uint32_t nbuckets, uint32_t init_cap, uint32_t max_entries);
void     bq_free(bq_t *q);

int      bq_insert(bq_t *q, int32_t key, int32_t val);
int      bq_remove(bq_t *q, int32_t key);
int      bq_get(const bq_t *q, int32_t key, int32_t *out);

int      bq_incr(bq_t *q, int32_t key, int32_t *out); /* +1 */
int      bq_decr(bq_t *q, int32_t key, int32_t *out); /* -1 */

int      bq_min(const bq_t *q, int32_t *out);
int      bq_max(const bq_t *q, int32_t *out);
uint32_t bq_size(const bq_t *q);

/* ascending iteration */
int      bq_first_bucket(const bq_t *q, int32_t *out);
int      bq_next_bucket(const bq_t *q, int32_t after, int32_t *out);
/* descending iteration */
int      bq_last_bucket(const bq_t *q, int32_t *out);
int      bq_prev_bucket(const bq_t *q, int32_t before, int32_t *out);
/* elements in a bucket */
uint32_t bq_bucket_head(const bq_t *q, int32_t val);
uint32_t bq_node_next(const bq_t *q, uint32_t node);
int32_t  bq_node_key(const bq_t *q, uint32_t node);

/* ================================================================
 * Persistence — save/load to/from a file
 *
 * Binary format:
 *   [0..3]   magic       (0x42510001)
 *   [4..7]   nbuckets
 *   [8..11]  max_entries
 *   [12..15] count       (number of entries)
 *   [16..]   count * (key int32_t + val int32_t) = 8B per entry
 *
 * bq_save — writes the bq_t state to a file (overwrites)
 * bq_load — reads a file into a fresh bq_t (q must be uninitialized)
 * ================================================================ */

#define BQ_FILE_MAGIC 0x42510001u

int      bq_save(const bq_t *q, const char *path);
int      bq_load(bq_t *q, const char *path);

/* ================================================================
 * BqManager - keeps one bq_t per table
 * ================================================================ */

#define BQ_MGR_MAX_TABLES 64

typedef struct {
    int32_t  tableId;
    bq_t     queue;
    int8_t   active;  /* 1 = used, 0 = free */
} BqTableEntry;

typedef struct {
    BqTableEntry tables[BQ_MGR_MAX_TABLES];
    uint32_t     default_nbuckets;    /* default number of buckets for a new table */
    uint32_t     default_max_entries; /* default max buffer size */
} BqManager;

void     bq_mgr_init(BqManager *mgr, uint32_t default_nbuckets, uint32_t default_max_entries);
void     bq_mgr_free(BqManager *mgr);
bq_t*    bq_mgr_get(BqManager *mgr, int32_t tableId);
int      bq_mgr_add_table(BqManager *mgr, int32_t tableId);
int      bq_mgr_remove_table(BqManager *mgr, int32_t tableId);
int8_t   bq_mgr_exists(BqManager *mgr, int32_t tableId);

#endif /* BQ_H */
