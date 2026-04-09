#include <stdio.h>
#include "memory-mgmt/memory-mgmt/all_table.h"
#include "memory-mgmt/memory-mgmt/tuple.h"
#include "memory-mgmt/memory-mgmt/all_var.h"
#include "memory-mgmt/memory-mgmt/type_ids.h"

int main(void) {
    /* --- 1. Inicjalizacja tabeli --- */
    AllTable table;
    all_table_init(&table, 2000);
    printf("=== AllTable zainicjalizowana ===\n\n");

    /* --- 2. Dodaj kilka krotek --- */
    int i;
    for (i = 0; i < 3; i++) {
        Tuple t;
        tuple_init(&t);

        /* Wartości kolumn: id (int32), wiek (int64), imie (string) */
        AllVar vals[3];
        vals[0] = all_var_from_int32(i + 1);
        vals[1] = all_var_from_int64(20 + i);
        vals[2] = all_var_from_string(i == 0 ? "Ala" : i == 1 ? "Bob" : "Ewa");

        int8_t bm[3] = {0, 0, 0}; /* brak NULLi */

        tuple_set(&t,
                  /*xmin=*/100 + i, /*xmax=*/0,
                  /*cid=*/i, /*infomask=*/0,
                  /*hoff=*/35, /*bitmap=*/0, /*oid=*/1000 + i,
                  bm, 3,
                  vals, 3);

        int rc = all_table_add(&table, &t,
                               /*nextblock=*/0, /*block_id=*/0,
                               /*pd_lsn=*/0, /*pd_checksum=*/0,
                               /*pd_flags=*/0, /*contain_toast=*/0);
        printf("Dodano krotkę %d: %s\n", i + 1, rc == 0 ? "OK" : "BLAD");
    }

    /* --- 3. Pokaż zawartość tabeli --- */
    printf("\n=== Zawartość AllTable ===\n");
    all_table_show(&table);

    /* --- 4. Serializacja i deserializacja --- */
    printf("\n=== Serializacja -> Deserializacja ===\n");
    int32_t size = 0;
    uint8_t *buf = all_table_marshal(&table, &size);
    printf("Zserializowano: %d bajtów\n", size);

    AllTable table2;
    all_table_unmarshal(&table2, buf, size);
    printf("Odczytano z bufora:\n");
    all_table_show(&table2);

    /* --- 5. Sprzątanie --- */
    free(buf);
    all_table_free(&table);
    all_table_free(&table2);

    printf("\nGotowe.\n");
    return 0;
}
