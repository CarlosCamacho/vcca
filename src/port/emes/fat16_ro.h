#ifndef EMESDOS09_FAT16_RO_H
#define EMESDOS09_FAT16_RO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EMES_FAT16_SECTOR_SIZE 512u
#define EMES_FAT16_MAX_PATH 80u

typedef bool (*emes_fat16_sector_reader_t)(uint32_t lba, uint8_t *sector, void *context);

typedef enum {
    EMES_FAT16_OK = 0,
    EMES_FAT16_IO,
    EMES_FAT16_INVALID,
    EMES_FAT16_NOT_FOUND,
    EMES_FAT16_PATH_NOT_FOUND,
    EMES_FAT16_NO_MORE,
    EMES_FAT16_SEEK
} emes_fat16_result_t;

typedef struct {
    emes_fat16_sector_reader_t read_sector;
    void *context;
    uint32_t partition_lba;
    uint32_t fat_lba;
    uint32_t root_lba;
    uint32_t data_lba;
    uint32_t total_sectors;
    uint32_t cluster_count;
    uint16_t reserved_sectors;
    uint16_t root_entries;
    uint16_t root_dir_sectors;
    uint16_t sectors_per_fat;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint8_t fat_count;
    uint8_t flags;
    uint32_t volume_serial;
    char volume_label[12];
} emes_fat16_volume_t;

typedef struct {
    uint8_t alias_raw[11];
    uint8_t attributes;
    uint8_t flags;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster;
    uint32_t size;
} emes_fat16_entry_t;

typedef struct {
    uint16_t first_cluster;
    uint32_t next_index;
    bool root;
    bool pending_lfn;
    uint8_t pattern[11];
} emes_fat16_dir_t;

typedef struct {
    emes_fat16_entry_t entry;
    uint32_t position;
} emes_fat16_file_t;

typedef struct {
    uint16_t bytes_per_sector;
    uint16_t free_clusters;
    uint16_t total_clusters;
    uint8_t sectors_per_cluster;
    uint32_t total_sectors;
} emes_fat16_free_t;

emes_fat16_result_t emes_fat16_mount(
    emes_fat16_volume_t *volume,
    emes_fat16_sector_reader_t reader,
    void *context
);
emes_fat16_result_t emes_fat16_open_dir(
    const emes_fat16_volume_t *volume,
    const char *path_pattern,
    emes_fat16_dir_t *directory
);
emes_fat16_result_t emes_fat16_dir_next(
    const emes_fat16_volume_t *volume,
    emes_fat16_dir_t *directory,
    emes_fat16_entry_t *entry
);
emes_fat16_result_t emes_fat16_open_path(
    const emes_fat16_volume_t *volume,
    const char *path,
    emes_fat16_file_t *file
);
emes_fat16_result_t emes_fat16_read(
    const emes_fat16_volume_t *volume,
    emes_fat16_file_t *file,
    uint8_t *destination,
    uint16_t requested,
    uint16_t *actual
);
emes_fat16_result_t emes_fat16_seek(
    emes_fat16_file_t *file,
    uint32_t offset,
    uint8_t whence
);
emes_fat16_result_t emes_fat16_free_space(
    const emes_fat16_volume_t *volume,
    emes_fat16_free_t *info
);

/* Sprint-6 compatibility wrappers. */
emes_fat16_result_t emes_fat16_root_next(
    const emes_fat16_volume_t *volume,
    emes_fat16_dir_t *directory,
    emes_fat16_entry_t *entry
);
emes_fat16_result_t emes_fat16_open_root(
    const emes_fat16_volume_t *volume,
    const char *path,
    emes_fat16_file_t *file
);

#endif
