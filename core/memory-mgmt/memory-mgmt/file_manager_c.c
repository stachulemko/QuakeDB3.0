#include "file_manager_c.h"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* Buduje pełną ścieżkę path/name.bin */
static void make_path(char *out, int out_size,
                      const char *path, const char *name) {
    snprintf(out, (size_t)out_size, "%s/%s.bin", path, name);
}

int fm_save_block(const char *path, const char *name,
                  const uint8_t *buf, int32_t len) {
    char full[512];
    FILE *f;
    make_path(full, sizeof(full), path, name);
    f = fopen(full, "wb");
    if (!f) { LOG_ERROR("fm_save_block: cannot open %s\n", full); return -1; }
    fwrite(buf, 1, (size_t)len, f);
    fclose(f);
    return 0;
}

int fm_load_block(const char *path, const char *name,
                  uint8_t *buf, int32_t buf_size, int32_t *out_len) {
    char full[512];
    FILE *f;
    long size;
    make_path(full, sizeof(full), path, name);
    f = fopen(full, "rb");
    if (!f) { LOG_ERROR("fm_load_block: cannot open %s\n", full); *out_len = -1; return -1; }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size > buf_size) size = buf_size;
    *out_len = (int32_t)fread(buf, 1, (size_t)size, f);
    fclose(f);
    return 0;
}

int fm_load_range(const char *path, const char *name,
                  int32_t start_offset, int32_t bytes_to_read,
                  uint8_t *buf) {
    char full[512];
    FILE *f;
    make_path(full, sizeof(full), path, name);
    f = fopen(full, "rb");
    if (!f) { LOG_ERROR("fm_load_range: cannot open %s\n", full); return -1; }
    fseek(f, start_offset, SEEK_SET);
    size_t r = fread(buf, 1, (size_t)bytes_to_read, f); (void)r;
    fclose(f);
    return 0;
}

int fm_save_block_at(const char *path, int32_t table_id,
                     const uint8_t buf[BLOCK_SIZE], int32_t block_num) {
    char name[32];
    char full[512];
    FILE *f;
    snprintf(name, sizeof(name), "%d", table_id);
    make_path(full, sizeof(full), path, name);
    f = fopen(full, "r+b");
    if (!f) { LOG_ERROR("fm_save_block_at: cannot open %s\n", full); return -1; }
    fseek(f, (long)block_num * BLOCK_SIZE, SEEK_SET);
    fwrite(buf, 1, BLOCK_SIZE, f);
    fclose(f);
    return 0;
}

int fm_create(const char *path, const char *name) {
    char full[512];
    FILE *f;
    make_path(full, sizeof(full), path, name);
    if (fm_exists(path, name)) {
        LOG_ERROR("fm_create: file already exists: %s\n", full);
        return -1;
    }
    f = fopen(full, "wb");
    if (!f) { LOG_ERROR("fm_create: cannot create %s\n", full); return -1; }
    fclose(f);
    return 0;
}

int32_t fm_file_size(const char *path, const char *name) {
    char full[512];
    struct stat st;
    make_path(full, sizeof(full), path, name);
    if (stat(full, &st) != 0) return -1;
    return (int32_t)st.st_size;
}

uint8_t *fm_get_block(const char *path, int32_t table_id, int32_t block_num) {
    char name[32];
    char full[512];
    FILE *f;
    uint8_t *buf;
    snprintf(name, sizeof(name), "%d", table_id);
    make_path(full, sizeof(full), path, name);
    f = fopen(full, "rb");
    if (!f) { LOG_ERROR("fm_get_block: cannot open %s\n", full); return NULL; }
    fseek(f, (long)block_num * BLOCK_SIZE, SEEK_SET);
    buf = (uint8_t *)malloc(BLOCK_SIZE);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, BLOCK_SIZE, f) != BLOCK_SIZE) {
        LOG_ERROR("fm_get_block: block %d out of range in %s\n", block_num, full);
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    return buf;
}

int fm_exists(const char *path, const char *name) {
    char full[512];
    FILE *f;
    make_path(full, sizeof(full), path, name);
    f = fopen(full, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}
