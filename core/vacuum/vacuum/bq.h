/* bq.h - bucket queue: klucz int32_t (numer bloku), wartosc int32_t.
 *
 * Konfigurowalny rozmiar kubelkow (nbuckets) ustawiany w bq_init.
 * Wszystkie operacje O(1) w najgorszym przypadku (poza rehashem mapy).
 *
 *   bq_insert / bq_remove / bq_get / bq_incr / bq_decr / bq_min / bq_max
 *
 * Struktura wewnetrzna:
 *   head[nbuckets]  - jeden kubelek na kazda mozliwa wartosc
 *   arena           - wezly z intruzywna lista dwukierunkowa (indeksy, nie wskazniki)
 *   bitmapa 3-poziomowa (dynamiczna) - szybkie znajdowanie niepustego kubelka
 *   hash mapa       - klucz int32_t -> indeks wezla w arenie
 *
 * BqManager - trzyma osobne bq_t dla kazdej tabeli z konfigurowalnym rozmiarem buffora.
 */
#ifndef BQ_H
#define BQ_H

#include <stdint.h>
#include <stddef.h>

#define BQ_NIL      0xFFFFFFFFu

/* kody bledow */
#define BQ_OK        0
#define BQ_ENOTFOUND (-1)  /* nie ma takiego klucza */
#define BQ_EEXISTS   (-2)  /* klucz juz istnieje */
#define BQ_ERANGE    (-3)  /* wartosc poza zakresem [0, nbuckets-1] */
#define BQ_ENOMEM    (-4)

typedef struct {
    /* --- konfiguracja --- */
    uint32_t  nbuckets;   /* ilosc kubelkow (max wartosc + 1)     */
    uint32_t  max_entries;/* maksymalny rozmiar buffora (0 = bez limitu) */

    /* --- arena wezlow --- */
    int32_t  *key;        /* numer bloku                           */
    int32_t  *val;        /* aktualna wartosc (ilosc dead tuples)  */
    uint32_t *next;       /* nastepny wezel w kubelku (lub free)   */
    uint32_t *prev;       /* poprzedni wezel w kubelku             */
    uint32_t  cap;        /* pojemnosc areny                       */
    uint32_t  used;       /* ile wezlow zajetych                   */
    uint32_t  free_head;

    /* --- kubelki --- */
    uint32_t *head;       /* [nbuckets] glowa listy elementow     */

    /* --- bitmapa niepustych kubelkow (dynamiczna) --- */
    uint64_t *l0;         /* 1 bit na kubelek         (l0_words)   */
    uint64_t *l1;         /* 1 bit na slowo l0        (l1_words)   */
    uint64_t  l2;         /* 1 bit na slowo l1        (max 64)     */
    uint32_t  l0_words;   /* ceil(nbuckets / 64)                   */
    uint32_t  l1_words;   /* ceil(l0_words / 64)                   */

    /* --- hash mapa: klucz -> indeks wezla --- */
    uint32_t *slot;       /* BQ_NIL = pusty, BQ_TOMB = nagrobek    */
    uint32_t  smask;      /* rozmiar-1, rozmiar jest potega dwojki */
    uint32_t  scount;     /* zajete + nagrobki                     */
} bq_t;

/* nbuckets = ilosc kubelkow (zakres wartosci 0..nbuckets-1)
 * init_cap = poczatkowa pojemnosc areny
 * max_entries = maks ilosc wpisow (0 = bez limitu) */
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

/* iteracja rosnaco */
int      bq_first_bucket(const bq_t *q, int32_t *out);
int      bq_next_bucket(const bq_t *q, int32_t after, int32_t *out);
/* iteracja malejaco */
int      bq_last_bucket(const bq_t *q, int32_t *out);
int      bq_prev_bucket(const bq_t *q, int32_t before, int32_t *out);
/* elementy w kubelku */
uint32_t bq_bucket_head(const bq_t *q, int32_t val);
uint32_t bq_node_next(const bq_t *q, uint32_t node);
int32_t  bq_node_key(const bq_t *q, uint32_t node);

/* ================================================================
 * Persistencja — zapis/odczyt z pliku
 *
 * Format binarny:
 *   [0..3]   magic       (0x42510001)
 *   [4..7]   nbuckets
 *   [8..11]  max_entries
 *   [12..15] count       (ilosc wpisow)
 *   [16..]   count * (key int32_t + val int32_t) = 8B per entry
 *
 * bq_save — zapisuje stan bq_t do pliku (nadpisuje)
 * bq_load — wczytuje z pliku do swiezego bq_t (q musi byc niezainicjalizowany)
 * ================================================================ */

#define BQ_FILE_MAGIC 0x42510001u

int      bq_save(const bq_t *q, const char *path);
int      bq_load(bq_t *q, const char *path);

/* ================================================================
 * BqManager - trzyma bq_t per tabela
 * ================================================================ */

#define BQ_MGR_MAX_TABLES 64

typedef struct {
    int32_t  tableId;
    bq_t     queue;
    int8_t   active;  /* 1 = uzywany, 0 = wolny */
} BqTableEntry;

typedef struct {
    BqTableEntry tables[BQ_MGR_MAX_TABLES];
    uint32_t     default_nbuckets;    /* domyslna ilosc kubelkow dla nowej tabeli */
    uint32_t     default_max_entries; /* domyslny max rozmiar buffora */
} BqManager;

void     bq_mgr_init(BqManager *mgr, uint32_t default_nbuckets, uint32_t default_max_entries);
void     bq_mgr_free(BqManager *mgr);
bq_t*    bq_mgr_get(BqManager *mgr, int32_t tableId);
int      bq_mgr_add_table(BqManager *mgr, int32_t tableId);
int      bq_mgr_remove_table(BqManager *mgr, int32_t tableId);
int8_t   bq_mgr_exists(BqManager *mgr, int32_t tableId);

#endif /* BQ_H */
