//
// Created by stas on 13.10.2026.
//

#ifndef QUAKEDB3_0_XID32RECYCLER_H
#define QUAKEDB3_0_XID32RECYCLER_H

#include <stdint.h>

/*
 * xid recycling (wraparound) in the spirit of Postgres:
 *   - the 32-bit xid counter wraps around and starts again from the beginning,
 *   - xids are compared "on a circle" (modulo 2^32): every xid sees 2^31 xids in the past
 *     and 2^31 xids in the future,
 *   - vacuum freezes old tuples (t_xmin = XID_FROZEN), so nobody needs their old xid anymore
 *     and it can be given to a new transaction,
 *   - beginTransaction refuses new xids before the counter catches up with the oldest xid
 *     that may still be in a tuple (oldestUnfrozen).
 *
 * Reserved values — the counter never gives them to a transaction:
 *   XID_NONE_NEG (-1)  "no xmax", like 0 (XID_NONE),
 *   XID_FROZEN         frozen xmin: older than every other xid, visible to everybody.
 */

#define XID_NONE      0
#define XID_NONE_NEG  (-1)
#define XID_FROZEN    INT32_MIN

struct xid32Recycler {
    int32_t oldestUnfrozen;   // the oldest xid that may still be in a tuple (everything older is frozen)
    int8_t  needFreeze;       // set by beginTransaction — the next autoVacuumScan freezes all tables
};

/* can the counter give this xid to a transaction */
static inline int8_t xidIsNormal(int32_t xid) {
    return xid != XID_NONE_NEG && xid != XID_FROZEN;
}

/* 1 if xid a is older than xid b (comparison modulo 2^32, XID_FROZEN is older than everything) */
static inline int8_t xidBefore(int32_t a, int32_t b) {
    if (a == XID_FROZEN) return b != XID_FROZEN;
    if (b == XID_FROZEN) return 0;
    return (int32_t)((uint32_t)a - (uint32_t)b) < 0;
}

/* the xid after `xid`, skipping the reserved values (unsigned — no signed overflow at INT32_MAX) */
static inline int32_t xidNext(int32_t xid) {
    uint32_t next = (uint32_t)xid + 1;
    while (!xidIsNormal((int32_t)next)) next++;
    return (int32_t)next;
}

/* how many xids `xid` is ahead of `older` */
static inline uint32_t xidDistance(int32_t older, int32_t xid) {
    return (uint32_t)xid - (uint32_t)older;
}

#endif //QUAKEDB3_0_XID32RECYCLER_H