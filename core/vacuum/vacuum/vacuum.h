//
// Created by stas on 9.09.2026.
//

#ifndef QUAKEDB3_0_VACUUM_H
#define QUAKEDB3_0_VACUUM_H

struct Vacuum {
    int32_t tableId;

    int32_t sumOfAllDeadTupleInsideTable;
    int32_t avgWidthTuple;
    int32_t densityDeadTuplesPerBlock;
};









#endif //QUAKEDB3_0_VACUUM_H
