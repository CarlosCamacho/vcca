#ifndef EMESDOS09_FAT16_RW_H
#define EMESDOS09_FAT16_RW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fat16_ro.h"

#define EMES_FAT16_RW_MAX_CLUSTERS 256u

typedef bool (*emes_fat16_sector_writer_t)(uint32_t lba, const uint8_t *sector, void *context);

typedef enum {
    EMES_FAT16_COMMIT_AFTER_DATA = 0,
    EMES_FAT16_COMMIT_AFTER_FAT_COPY_0,
    EMES_FAT16_COMMIT_AFTER_FAT_COPY_1,
    EMES_FAT16_COMMIT_AFTER_DIRECTORY,
    EMES_FAT16_COMMIT_AFTER_CLEANUP,
    EMES_FAT16_COMMIT_AFTER_VERIFY
} emes_fat16_commit_boundary_t;

typedef bool (*emes_fat16_boundary_hook_t)(emes_fat16_commit_boundary_t boundary, void *context);

typedef enum {
    EMES_FAT16_RW_OK = 0,
    EMES_FAT16_RW_IO,
    EMES_FAT16_RW_INVALID,
    EMES_FAT16_RW_NO_SPACE,
    EMES_FAT16_RW_WRITE_PROTECT,
    EMES_FAT16_RW_POWER_LOSS,
    EMES_FAT16_RW_VERIFY_FAILED
} emes_fat16_rw_result_t;

typedef struct {
    uint32_t directory_lba;
    uint16_t directory_offset;
    uint16_t old_first_cluster;
    uint8_t alias_raw[11];
    uint8_t attributes;
    uint16_t create_time;
    uint16_t create_date;
    const uint8_t *data;
    uint32_t size;
    bool verify;
    bool write_protect;
} emes_fat16_commit_request_t;

typedef struct {
    uint16_t first_cluster;
    uint16_t allocated_clusters;
    uint16_t released_clusters;
    uint16_t data_cluster_writes;
    uint16_t fat_entry_writes;
    uint16_t directory_entry_writes;
    uint16_t verify_passes;
    emes_fat16_commit_boundary_t last_boundary;
} emes_fat16_commit_result_t;

/*
 * Commit a complete replacement file image using metadata-last copy-on-write.
 * The caller resolves the DOS path and supplies the short-directory-entry
 * location. The engine allocates fresh clusters, writes all data, publishes
 * every FAT copy, publishes the directory record, and only then releases the
 * old chain. Directory growth is a Sprint 10 responsibility.
 */
emes_fat16_rw_result_t emes_fat16_rw_commit(
    const emes_fat16_volume_t *volume,
    emes_fat16_sector_writer_t writer,
    void *io_context,
    const emes_fat16_commit_request_t *request,
    emes_fat16_boundary_hook_t boundary_hook,
    void *boundary_context,
    emes_fat16_commit_result_t *result
);

#endif
