#include <stdio.h>
#include "bufforing-stm/bufforing-stm/dataBuffor.h"

int main(void) {
    Buffors buffors;

    FSMCache fsmCache;

    FSMMapAll fsmMapAll;

    init_FSMMapAll(&fsmMapAll);

    fsm_cache_init(&fsmCache);

    initializeBuffors(&buffors, 3);

    addTable(&fsmMapAll,&buffors,&fsmCache,1,(int8_t[]){ID_INT32, ID_STRING},(int8_t[]){0, 1},(char[2][MAX_COL_NAME_LEN]){{"id"}, {"name"}});

    showBuffors(&buffors);

    addTuple(&buffors,&fsmCache,&fsmMapAll,1,
             (AllVar[]){all_var_from_int32(42), all_var_from_string("Alice")}, 2,
             (int8_t[]){0, 0}, 2);
    //addDataToFSMMapAllAndReturnBufforToAdd(buffors,fsmCache,fsmMapAll,1,,BLOCK_USABLE_SIZE)
    showBuffors(&buffors);
}
