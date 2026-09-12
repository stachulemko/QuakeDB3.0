#include "bq.h"
#include <stdlib.h>
#include <string.h>

#define BQ_TOMB 0xFFFFFFFEu

/* ------------------------------------------------------------------ */
/* bit tricks                                                          */
/* ------------------------------------------------------------------ */
#if defined(__GNUC__) || defined(__clang__)
static inline uint32_t bq__ctz64(uint64_t x) { return (uint32_t)__builtin_ctzll(x); }
static inline uint32_t bq__clz64(uint64_t x) { return (uint32_t)__builtin_clzll(x); }
#else
static inline uint32_t bq__ctz64(uint64_t x) {
    uint32_t n = 0; while (!(x & 1u)) { x >>= 1; n++; } return n;
}
static inline uint32_t bq__clz64(uint64_t x) {
    uint32_t n = 0; while (!(x >> 63)) { x <<= 1; n++; } return n;
}
#endif

/* wartosc -> indeks kubelka (wartosci sa >= 0, wiec mapowanie 1:1) */
#define IDX(v)   ((uint32_t)(v))
#define UNIDX(i) ((int32_t)(i))

/* ------------------------------------------------------------------ */
/* bitmapa niepustych kubelkow                                         */
/* ------------------------------------------------------------------ */
static void bq__bm_set(bq_t *q, uint32_t b) {
    uint32_t w0 = b >> 6, w1 = w0 >> 6;
    q->l0[w0] |= 1ULL << (b & 63);
    q->l1[w1] |= 1ULL << (w0 & 63);
    q->l2     |= 1ULL << w1;
}

static void bq__bm_clear(bq_t *q, uint32_t b) {
    uint32_t w0 = b >> 6, w1 = w0 >> 6;
    q->l0[w0] &= ~(1ULL << (b & 63));
    if (q->l0[w0]) return;
    q->l1[w1] &= ~(1ULL << (w0 & 63));
    if (q->l1[w1]) return;
    q->l2 &= ~(1ULL << w1);
}

/* pierwsze zapalone slowo l0 o indeksie >= from */
static uint32_t bq__next_word(const bq_t *q, uint32_t from) {
    uint32_t w, off, i2;
    uint64_t x;
    if (from >= q->l1_words) return q->l0_words;
    w = from >> 6; off = from & 63;
    if (w < q->l1_words) {
        x = q->l1[w] & (~0ULL << off);
        if (x) return (w << 6) | bq__ctz64(x);
    }
    if (w + 1 >= q->l1_words) {
        x = q->l2 & (~0ULL << (w + 1));
        if (!x) return q->l0_words;
        i2 = bq__ctz64(x);
        if (i2 >= q->l1_words) return q->l0_words;
        return (i2 << 6) | bq__ctz64(q->l1[i2]);
    }
    x = q->l2 & (~0ULL << (w + 1));
    if (!x) return q->l0_words;
    i2 = bq__ctz64(x);
    if (i2 >= q->l1_words) return q->l0_words;
    return (i2 << 6) | bq__ctz64(q->l1[i2]);
}

/* pierwszy niepusty kubelek o indeksie >= from */
static uint32_t bq__next_set(const bq_t *q, uint32_t from) {
    uint32_t w, off, nw;
    uint64_t x;
    if (from >= q->nbuckets) return q->nbuckets;
    w = from >> 6; off = from & 63;
    if (w < q->l0_words) {
        x = q->l0[w] & (~0ULL << off);
        if (x) return (w << 6) | bq__ctz64(x);
    }
    nw = bq__next_word(q, w + 1);
    if (nw >= q->l0_words) return q->nbuckets;
    return (nw << 6) | bq__ctz64(q->l0[nw]);
}

/* ostatni niepusty kubelek o indeksie <= from */
static uint32_t bq__prev_set(const bq_t *q, uint32_t from) {
    /* szukaj od from w dol */
    uint32_t w = from >> 6;
    uint64_t x;
    if (w < q->l0_words) {
        uint32_t off = from & 63;
        x = q->l0[w] & (~0ULL >> (63 - off));
        if (x) return (w << 6) | (63 - bq__clz64(x));
    }
    /* szukaj w nizszych slowach */
    if (w == 0) return q->nbuckets;
    for (uint32_t i = w; i > 0; i--) {
        if (i - 1 < q->l0_words && q->l0[i - 1]) {
            return ((i - 1) << 6) | (63 - bq__clz64(q->l0[i - 1]));
        }
    }
    return q->nbuckets;
}

/* ------------------------------------------------------------------ */
/* hash mapa: int32_t -> indeks wezla                                  */
/* ------------------------------------------------------------------ */
static inline uint32_t bq__hash(int32_t k) {
    uint32_t x = (uint32_t)k;
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static uint32_t bq__find_slot(const bq_t *q, int32_t key) {
    uint32_t i = bq__hash(key) & q->smask;
    for (;;) {
        uint32_t n = q->slot[i];
        if (n == BQ_NIL) return BQ_NIL;
        if (n != BQ_TOMB && q->key[n] == key) return i;
        i = (i + 1) & q->smask;
    }
}

static int bq__map_grow(bq_t *q) {
    uint32_t old_size = q->smask + 1, new_size = old_size * 2, i;
    uint32_t *ns = (uint32_t *)malloc((size_t)new_size * sizeof(uint32_t));
    if (!ns) return BQ_ENOMEM;
    memset(ns, 0xFF, (size_t)new_size * sizeof(uint32_t));
    for (i = 0; i < old_size; i++) {
        uint32_t n = q->slot[i], j;
        if (n == BQ_NIL || n == BQ_TOMB) continue;
        j = bq__hash(q->key[n]) & (new_size - 1);
        while (ns[j] != BQ_NIL) j = (j + 1) & (new_size - 1);
        ns[j] = n;
    }
    free(q->slot);
    q->slot = ns;
    q->smask = new_size - 1;
    q->scount = q->used;
    return BQ_OK;
}

static int bq__map_put(bq_t *q, int32_t key, uint32_t node) {
    uint32_t i, first_tomb = BQ_NIL;
    if ((q->scount + 1) * 4 >= (q->smask + 1) * 3) {
        int rc = bq__map_grow(q);
        if (rc != BQ_OK) return rc;
    }
    i = bq__hash(key) & q->smask;
    for (;;) {
        uint32_t n = q->slot[i];
        if (n == BQ_NIL) {
            if (first_tomb != BQ_NIL) { q->slot[first_tomb] = node; }
            else { q->slot[i] = node; q->scount++; }
            return BQ_OK;
        }
        if (n == BQ_TOMB) { if (first_tomb == BQ_NIL) first_tomb = i; }
        else if (q->key[n] == key) return BQ_EEXISTS;
        i = (i + 1) & q->smask;
    }
}

/* ------------------------------------------------------------------ */
/* arena                                                               */
/* ------------------------------------------------------------------ */
static int bq__arena_grow(bq_t *q) {
    uint32_t nc = q->cap ? q->cap * 2 : 16, i;
    int32_t  *k;
    int32_t  *v;
    uint32_t *nx, *pv;

    /* jesli max_entries > 0, nie rosniemy ponad limit */
    if (q->max_entries > 0 && nc > q->max_entries) nc = q->max_entries;
    if (nc <= q->cap) return BQ_ENOMEM;

    k = (int32_t *)realloc(q->key, (size_t)nc * sizeof(int32_t));
    if (!k) return BQ_ENOMEM;
    q->key = k;

    v = (int32_t *)realloc(q->val, (size_t)nc * sizeof(int32_t));
    if (!v) return BQ_ENOMEM;
    q->val = v;

    nx = (uint32_t *)realloc(q->next, (size_t)nc * sizeof(uint32_t));
    if (!nx) return BQ_ENOMEM;
    q->next = nx;

    pv = (uint32_t *)realloc(q->prev, (size_t)nc * sizeof(uint32_t));
    if (!pv) return BQ_ENOMEM;
    q->prev = pv;

    for (i = nc; i > q->cap; i--) {
        q->next[i - 1] = q->free_head;
        q->free_head = i - 1;
    }
    q->cap = nc;
    return BQ_OK;
}

static uint32_t bq__node_alloc(bq_t *q) {
    uint32_t n;
    if (q->free_head == BQ_NIL && bq__arena_grow(q) != BQ_OK) return BQ_NIL;
    n = q->free_head;
    q->free_head = q->next[n];
    return n;
}

/* ------------------------------------------------------------------ */
/* wpinanie / wypinanie wezla z kubelka                                */
/* ------------------------------------------------------------------ */
static void bq__link(bq_t *q, uint32_t n, int32_t val) {
    uint32_t b = IDX(val), h = q->head[b];
    q->prev[n] = BQ_NIL;
    q->next[n] = h;
    if (h != BQ_NIL) q->prev[h] = n;
    else             bq__bm_set(q, b);
    q->head[b] = n;
    q->val[n] = val;
}

static void bq__unlink(bq_t *q, uint32_t n) {
    uint32_t b = IDX(q->val[n]);
    uint32_t p = q->prev[n], nx = q->next[n];
    if (p != BQ_NIL) q->next[p] = nx;
    else             q->head[b] = nx;
    if (nx != BQ_NIL) q->prev[nx] = p;
    if (q->head[b] == BQ_NIL) bq__bm_clear(q, b);
}

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */
int bq_init(bq_t *q, uint32_t nbuckets, uint32_t init_cap, uint32_t max_entries) {
    uint32_t sz = 16;
    if (nbuckets == 0) return BQ_ERANGE;

    /* clamp init_cap do max_entries jesli ustawiony */
    if (max_entries > 0 && init_cap > max_entries) init_cap = max_entries;

    memset(q, 0, sizeof(*q));
    q->nbuckets    = nbuckets;
    q->max_entries = max_entries;
    q->free_head   = BQ_NIL;

    /* oblicz rozmiar bitmap */
    q->l0_words = (nbuckets + 63) / 64;
    q->l1_words = (q->l0_words + 63) / 64;

    /* alokuj kubelki */
    q->head = (uint32_t *)malloc((size_t)nbuckets * sizeof(uint32_t));
    if (!q->head) return BQ_ENOMEM;
    memset(q->head, 0xFF, (size_t)nbuckets * sizeof(uint32_t));

    /* alokuj bitmapy */
    q->l0 = (uint64_t *)calloc(q->l0_words, sizeof(uint64_t));
    if (!q->l0) { free(q->head); return BQ_ENOMEM; }

    q->l1 = (uint64_t *)calloc(q->l1_words, sizeof(uint64_t));
    if (!q->l1) { free(q->head); free(q->l0); return BQ_ENOMEM; }

    q->l2 = 0;

    /* hash mapa */
    while (sz < init_cap * 2) sz <<= 1;
    q->slot = (uint32_t *)malloc((size_t)sz * sizeof(uint32_t));
    if (!q->slot) { free(q->head); free(q->l0); free(q->l1); return BQ_ENOMEM; }
    memset(q->slot, 0xFF, (size_t)sz * sizeof(uint32_t));
    q->smask = sz - 1;

    /* arena */
    while (q->cap < init_cap) {
        if (bq__arena_grow(q) != BQ_OK) { bq_free(q); return BQ_ENOMEM; }
    }
    return BQ_OK;
}

void bq_free(bq_t *q) {
    free(q->key); free(q->val); free(q->next); free(q->prev);
    free(q->head); free(q->l0); free(q->l1); free(q->slot);
    memset(q, 0, sizeof(*q));
}

int bq_insert(bq_t *q, int32_t key, int32_t val) {
    uint32_t n;
    if (val < 0 || (uint32_t)val >= q->nbuckets) return BQ_ERANGE;
    if (bq__find_slot(q, key) != BQ_NIL) return BQ_EEXISTS;
    if (q->max_entries > 0 && q->used >= q->max_entries) return BQ_ENOMEM;
    n = bq__node_alloc(q);
    if (n == BQ_NIL) return BQ_ENOMEM;
    q->key[n] = key;
    bq__link(q, n, val);
    if (bq__map_put(q, key, n) != BQ_OK) { bq__unlink(q, n); return BQ_ENOMEM; }
    q->used++;
    return BQ_OK;
}

int bq_remove(bq_t *q, int32_t key) {
    uint32_t i = bq__find_slot(q, key), n;
    if (i == BQ_NIL) return BQ_ENOTFOUND;
    n = q->slot[i];
    bq__unlink(q, n);
    q->slot[i] = BQ_TOMB;
    q->next[n] = q->free_head;
    q->free_head = n;
    q->used--;
    return BQ_OK;
}

int bq_get(const bq_t *q, int32_t key, int32_t *out) {
    uint32_t i = bq__find_slot(q, key);
    if (i == BQ_NIL) return BQ_ENOTFOUND;
    if (out) *out = q->val[q->slot[i]];
    return BQ_OK;
}

/* rdzen: przeniesienie wezla do sasiedniego kubelka */
static int bq__move(bq_t *q, int32_t key, int delta, int32_t *out) {
    uint32_t i = bq__find_slot(q, key), n;
    int64_t nv;
    if (i == BQ_NIL) return BQ_ENOTFOUND;
    n = q->slot[i];
    nv = (int64_t)q->val[n] + delta;
    if (nv < 0 || (uint32_t)nv >= q->nbuckets) return BQ_ERANGE;
    bq__unlink(q, n);
    bq__link(q, n, (int32_t)nv);
    if (out) *out = (int32_t)nv;
    return BQ_OK;
}

int bq_incr(bq_t *q, int32_t key, int32_t *out) { return bq__move(q, key, +1, out); }
int bq_decr(bq_t *q, int32_t key, int32_t *out) { return bq__move(q, key, -1, out); }

int bq_min(const bq_t *q, int32_t *out) {
    uint32_t b = bq__next_set(q, 0);
    if (b >= q->nbuckets) return BQ_ENOTFOUND;
    if (out) *out = UNIDX(b);
    return BQ_OK;
}

int bq_max(const bq_t *q, int32_t *out) {
    if (!q->l2) return BQ_ENOTFOUND;
    /* znajdz ostatni niepusty kubelek */
    uint32_t i2 = 63 - bq__clz64(q->l2);
    if (i2 >= q->l1_words) return BQ_ENOTFOUND;
    uint32_t i1 = (i2 << 6) | (63 - bq__clz64(q->l1[i2]));
    if (i1 >= q->l0_words) return BQ_ENOTFOUND;
    uint32_t b  = (i1 << 6) | (63 - bq__clz64(q->l0[i1]));
    if (b >= q->nbuckets) return BQ_ENOTFOUND;
    if (out) *out = UNIDX(b);
    return BQ_OK;
}

uint32_t bq_size(const bq_t *q) { return q->used; }

int bq_first_bucket(const bq_t *q, int32_t *out) { return bq_min(q, out); }

int bq_next_bucket(const bq_t *q, int32_t after, int32_t *out) {
    uint32_t b = bq__next_set(q, IDX(after) + 1);
    if (b >= q->nbuckets) return BQ_ENOTFOUND;
    if (out) *out = UNIDX(b);
    return BQ_OK;
}

int bq_last_bucket(const bq_t *q, int32_t *out) { return bq_max(q, out); }

int bq_prev_bucket(const bq_t *q, int32_t before, int32_t *out) {
    uint32_t idx = IDX(before);
    if (idx == 0) return BQ_ENOTFOUND;
    uint32_t b = bq__prev_set(q, idx - 1);
    if (b >= q->nbuckets) return BQ_ENOTFOUND;
    if (out) *out = UNIDX(b);
    return BQ_OK;
}

uint32_t bq_bucket_head(const bq_t *q, int32_t val) {
    uint32_t b = IDX(val);
    if (b >= q->nbuckets) return BQ_NIL;
    return q->head[b];
}
uint32_t bq_node_next(const bq_t *q, uint32_t node)  { return q->next[node]; }
int32_t  bq_node_key(const bq_t *q, uint32_t node)   { return q->key[node]; }

/* ================================================================== */
/* Persistencja — bq_save / bq_load                                    */
/* ================================================================== */

#include <stdio.h>

static int bq__write32(FILE *f, uint32_t v) {
    return fwrite(&v, sizeof(v), 1, f) == 1 ? BQ_OK : BQ_ENOMEM;
}

static int bq__read32(FILE *f, uint32_t *v) {
    return fread(v, sizeof(*v), 1, f) == 1 ? BQ_OK : BQ_ENOTFOUND;
}

int bq_save(const bq_t *q, const char *path) {
    if (q == NULL || path == NULL) return BQ_ENOTFOUND;

    FILE *f = fopen(path, "wb");
    if (!f) return BQ_ENOMEM;

    /* header */
    if (bq__write32(f, BQ_FILE_MAGIC) != BQ_OK) goto fail;
    if (bq__write32(f, q->nbuckets)   != BQ_OK) goto fail;
    if (bq__write32(f, q->max_entries) != BQ_OK) goto fail;
    if (bq__write32(f, q->used)        != BQ_OK) goto fail;

    /* wpisy: iteruj przez arene — sloty z haszmapa wskazuja na wezly areny */
    for (uint32_t i = 0; i <= q->smask; i++) {
        uint32_t n = q->slot[i];
        if (n == BQ_NIL || n == BQ_TOMB) continue;
        int32_t k = q->key[n];
        int32_t v = q->val[n];
        if (fwrite(&k, sizeof(k), 1, f) != 1) goto fail;
        if (fwrite(&v, sizeof(v), 1, f) != 1) goto fail;
    }

    fclose(f);
    return BQ_OK;

fail:
    fclose(f);
    return BQ_ENOMEM;
}

int bq_load(bq_t *q, const char *path) {
    if (q == NULL || path == NULL) return BQ_ENOTFOUND;

    FILE *f = fopen(path, "rb");
    if (!f) return BQ_ENOTFOUND;

    uint32_t magic, nbuckets, max_entries, count;

    if (bq__read32(f, &magic)       != BQ_OK) goto fail;
    if (magic != BQ_FILE_MAGIC)                goto fail;
    if (bq__read32(f, &nbuckets)    != BQ_OK) goto fail;
    if (bq__read32(f, &max_entries) != BQ_OK) goto fail;
    if (bq__read32(f, &count)       != BQ_OK) goto fail;

    int rc = bq_init(q, nbuckets, count > 0 ? count : 16, max_entries);
    if (rc != BQ_OK) goto fail;

    for (uint32_t i = 0; i < count; i++) {
        int32_t k, v;
        if (fread(&k, sizeof(k), 1, f) != 1) { bq_free(q); goto fail; }
        if (fread(&v, sizeof(v), 1, f) != 1) { bq_free(q); goto fail; }
        rc = bq_insert(q, k, v);
        if (rc != BQ_OK) { bq_free(q); goto fail; }
    }

    fclose(f);
    return BQ_OK;

fail:
    fclose(f);
    return BQ_ENOTFOUND;
}

/* ================================================================== */
/* BqManager                                                           */
/* ================================================================== */

void bq_mgr_init(BqManager *mgr, uint32_t default_nbuckets, uint32_t default_max_entries) {
    memset(mgr, 0, sizeof(*mgr));
    mgr->default_nbuckets    = default_nbuckets;
    mgr->default_max_entries = default_max_entries;
}

void bq_mgr_free(BqManager *mgr) {
    for (int i = 0; i < BQ_MGR_MAX_TABLES; i++) {
        if (mgr->tables[i].active) {
            bq_free(&mgr->tables[i].queue);
            mgr->tables[i].active = 0;
        }
    }
}

bq_t* bq_mgr_get(BqManager *mgr, int32_t tableId) {
    for (int i = 0; i < BQ_MGR_MAX_TABLES; i++) {
        if (mgr->tables[i].active && mgr->tables[i].tableId == tableId) {
            return &mgr->tables[i].queue;
        }
    }
    return NULL;
}

int bq_mgr_add_table(BqManager *mgr, int32_t tableId) {
    /* sprawdz duplikat */
    if (bq_mgr_get(mgr, tableId) != NULL) return BQ_EEXISTS;

    /* znajdz wolny slot */
    for (int i = 0; i < BQ_MGR_MAX_TABLES; i++) {
        if (!mgr->tables[i].active) {
            mgr->tables[i].tableId = tableId;
            mgr->tables[i].active = 1;
            return bq_init(&mgr->tables[i].queue,
                           mgr->default_nbuckets,
                           16,
                           mgr->default_max_entries);
        }
    }
    return BQ_ENOMEM; /* brak wolnych slotow */
}

int bq_mgr_remove_table(BqManager *mgr, int32_t tableId) {
    for (int i = 0; i < BQ_MGR_MAX_TABLES; i++) {
        if (mgr->tables[i].active && mgr->tables[i].tableId == tableId) {
            bq_free(&mgr->tables[i].queue);
            mgr->tables[i].active = 0;
            return BQ_OK;
        }
    }
    return BQ_ENOTFOUND;
}

int8_t bq_mgr_exists(BqManager *mgr, int32_t tableId) {
    return bq_mgr_get(mgr, tableId) != NULL ? 1 : 0;
}
