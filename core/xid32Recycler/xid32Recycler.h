//
// Created by stas on 13.10.2026.
//

#ifndef QUAKEDB3_0_XID32RECYCLER_H
#define QUAKEDB3_0_XID32RECYCLER_H

#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "log.h"
#include "../../memory-mgmt/memory-mgmt/config.h"



struct xid32Recycler {

    size_t mapped_size;   // multiples of blockSize
};
void addXid(int32_t xidNum,int64_t val) { // and neXid get incremented
    // but wee need take last block where places are still
    key[xidNum] = val;
}
int8_t chekcIfBefore(int32_t xid1,int32_t xid2) {
    then we will return czy val jest wiekszy od xid2 to wtedy 0 jezlei nie 1 rosuzmiesz !
}








#endif //QUAKEDB3_0_XID32RECYCLER_H
