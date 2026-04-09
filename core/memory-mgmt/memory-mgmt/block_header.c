#include "block_header.h"
#include <stdio.h>

void block_header_init(BlockHeader *h) {
    h->nextblock    = -1;
    h->block_id     = -1;
    h->pd_lsn       = 0;
    h->pd_checksum  = 0;
    h->pd_flags     = 0;
    h->contain_toast = 0;
}

void block_header_set(BlockHeader *h, int32_t nextblock, int32_t block_id,
                      int32_t pd_lsn, int16_t pd_checksum,
                      int16_t pd_flags, int8_t contain_toast) {
    h->nextblock     = nextblock;
    h->block_id      = block_id;
    h->pd_lsn        = pd_lsn;
    h->pd_checksum   = pd_checksum;
    h->pd_flags      = pd_flags;
    h->contain_toast = contain_toast;
}

int block_header_marshal(uint8_t *buf, const BlockHeader *h) {
    int off = 0;
    off += marshal_int32(buf + off, h->nextblock);
    off += marshal_int32(buf + off, h->block_id);
    off += marshal_int32(buf + off, h->pd_lsn);
    off += marshal_int16(buf + off, h->pd_checksum);
    off += marshal_int16(buf + off, h->pd_flags);
    off += marshal_int8 (buf + off, h->contain_toast);
    return off; /* zawsze BLOCK_HEADER_SIZE = 17 */
}

void block_header_unmarshal(BlockHeader *h, const uint8_t *buf) {
    unmarshal_int32(&h->nextblock,     buf + 0);
    unmarshal_int32(&h->block_id,      buf + 4);
    unmarshal_int32(&h->pd_lsn,        buf + 8);
    unmarshal_int16(&h->pd_checksum,   buf + 12);
    unmarshal_int16(&h->pd_flags,      buf + 14);
    unmarshal_int8 (&h->contain_toast, buf + 16);
}

void block_header_show(const BlockHeader *h) {
    printf("nextblock:     %d\n", h->nextblock);
    printf("block_id:      %d\n", h->block_id);
    printf("pd_lsn:        %d\n", h->pd_lsn);
    printf("pd_checksum:   %d\n", h->pd_checksum);
    printf("pd_flags:      %d\n", h->pd_flags);
    printf("contain_toast: %d\n", (int)h->contain_toast);
}
