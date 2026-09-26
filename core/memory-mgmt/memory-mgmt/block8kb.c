#include "block8kb.h"
#include <string.h>
#include <stdio.h>

void block8kb_init(Block8kb *b, int32_t free_space,
                   int32_t nextblock, int32_t block_id,
                   int32_t pd_lsn, int16_t pd_checksum,
                   int16_t pd_flags, int8_t contain_toast) {
    memset(b, 0, sizeof(*b));
    b->free_space  = free_space;
    b->usable_size = BLOCK_SIZE - free_space;
    block_header_set(&b->header, nextblock, block_id,
                     pd_lsn, pd_checksum, pd_flags, contain_toast);
}

int32_t block8kb_used(const Block8kb *b) {
    int32_t i;
    /* 2 (ID_ALL_BLOCK) + BLOCK_HEADER_SIZE + sum of tuple sizes */
    int32_t total = 2 + BLOCK_HEADER_SIZE;
    for (i = 0; i < b->tuple_count; i++)
        total += tuple_size(&b->tuples[i]);
    return total;
}

/* First slot marked INFOMASK_UNUSED, or -1 if there is none */
static int32_t block8kb_free_slot(const Block8kb *b) {
    int32_t i;
    for (i = 0; i < b->tuple_count; i++)
        if (b->tuples[i].header.t_infomask & INFOMASK_UNUSED) return i;
    return -1;
}

int block8kb_full(const Block8kb *b, const Tuple *t) {
    int32_t slot = block8kb_free_slot(b);
    int32_t used = block8kb_used(b);
    /* free slot: the new tuple replaces the tombstone, so its size is subtracted */
    if (slot >= 0)
        return (used - tuple_size(&b->tuples[slot]) + tuple_size(t)) > b->usable_size;
    if (b->tuple_count >= MAX_TUPLES_PER_BLOCK) return 1;
    return (used + tuple_size(t)) > b->usable_size;
}

int block8kb_add(Block8kb *b, const Tuple *t) {
    int32_t slot;
    if (block8kb_full(b, t)) return -1;
    /* first free slot first — indices (TIDs) of the other tuples do not change */
    slot = block8kb_free_slot(b);
    if (slot >= 0) {
        b->tuples[slot] = *t;
        return slot;
    }
    b->tuples[b->tuple_count] = *t;
    return b->tuple_count++;
}

int block8kb_marshal(uint8_t buf[BLOCK_SIZE], const Block8kb *b) {
    int32_t i, zero_count;
    int off = 0;

    memset(buf, 0, BLOCK_SIZE);
    off += marshal_int16(buf + off, ID_ALL_BLOCK);
    off += block_header_marshal(buf + off, &b->header);

    for (i = 0; i < b->tuple_count; i++) {
        int written = tuple_marshal(buf + off, &b->tuples[i]);
        if (off + written > BLOCK_SIZE) break; /* safety guard */
        off += written;
    }

    /* zero padding (already done by memset) */
    zero_count = BLOCK_SIZE - off;
    (void)zero_count;

    return BLOCK_SIZE;
}

void block8kb_unmarshal(Block8kb *b, const uint8_t buf[BLOCK_SIZE]) {
    int16_t tag;
    int32_t off = 0;

    memset(b, 0, sizeof(*b));

    unmarshal_int16(&tag, buf + off); off += 2;
    if (tag != ID_ALL_BLOCK) return;

    block_header_unmarshal(&b->header, buf + off);
    off += BLOCK_HEADER_SIZE;

    while (off < BLOCK_SIZE - 6 && b->tuple_count < MAX_TUPLES_PER_BLOCK) {
        int16_t tuple_tag;
        int32_t payload_size;

        unmarshal_int16(&tuple_tag,    buf + off);
        unmarshal_int32(&payload_size, buf + off + 2);

        if (tuple_tag != ID_TUPLE) break;
        if (payload_size <= 0 || off + 6 + payload_size > BLOCK_SIZE) break;

        tuple_unmarshal(&b->tuples[b->tuple_count], buf + off,
                        6 + payload_size);
        off += 6 + payload_size;
        b->tuple_count++;
    }
}

void block8kb_show(const Block8kb *b) {
    int32_t i;
    block_header_show(&b->header);
    printf("tuple_count: %d\n", b->tuple_count);
    for (i = 0; i < b->tuple_count; i++) {
        printf("--- tuple %d ---\n", i);
        tuple_show(&b->tuples[i]);
    }
}
