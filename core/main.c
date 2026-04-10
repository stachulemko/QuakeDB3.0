#include <stdio.h>
#include "bufforing-stm/bufforing-stm/dataBuffor.h"

int main(void) {
    Buffors * buffors = NULL;

    FSMCache* fsmCache = NULL;

    FSMMapAll* fsmMapAll = NULL;

    fsm_cache_init(fsmCache);

    initializeBuffors(buffors,3);

    addTable(buffors,fsmCache,1,(int8_t[]){ID_INT32, ID_STRING},(int8_t[]){0, 1},(char[2][MAX_COL_NAME_LEN]){{"id"}, {"name"}});

    addTuple(buffors,fsmCache,fsmMapAll,1,(AllVar[]){all_var_from_int32(42), all_var_from_string("Alice")},(int8_t[]){0, 0});
    //addDataToFSMMapAllAndReturnBufforToAdd(buffors,fsmCache,fsmMapAll,1,,BLOCK_USABLE_SIZE)
}
