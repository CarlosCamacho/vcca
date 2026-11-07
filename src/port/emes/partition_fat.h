#ifndef EMESDOS09_PARTITION_FAT_H
#define EMESDOS09_PARTITION_FAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define EMES_PARTITION_SECTOR_SIZE 512u
#define EMES_PARTITION_MAX_PRIMARY 4u
typedef enum {
    EMES_FAT_NONE = 0,
    EMES_FAT12 = 12,
    EMES_FAT16 = 16,
    EMES_FAT32 = 32
} emes_fat_kind_t;
typedef struct {
    uint8_t index;
    bool bootable;
    uint8_t type;
    uint32_t first_lba;
    uint32_t sectors;
} emes_mbr_partition_t;
typedef struct {
    emes_fat_kind_t kind;
    bool read_only;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t root_entries;
    uint32_t sectors_per_fat;
    uint32_t total_sectors;
    uint32_t cluster_count;
} emes_fat_probe_t;
typedef struct {
    uint16_t root_entries;
    uint16_t root_sectors;
    uint16_t sectors_per_fat;
    uint8_t sectors_per_cluster;
} emes_fat_format_layout_t;
bool emes_mbr_enumerate(const uint8_t sector[EMES_PARTITION_SECTOR_SIZE],
                        uint32_t ide_sectors,
                        emes_mbr_partition_t out[EMES_PARTITION_MAX_PRIMARY],
                        size_t *count);
bool emes_mbr_create_primary(uint8_t sector[EMES_PARTITION_SECTOR_SIZE],
                             uint32_t ide_sectors, uint8_t index,
                             uint32_t first_lba, uint32_t sectors,
                             uint8_t type, bool bootable);
bool emes_fat_probe(const uint8_t boot[EMES_PARTITION_SECTOR_SIZE],
                    uint32_t partition_sectors, emes_fat_probe_t *out);
bool emes_fat_format_boot(uint8_t boot[EMES_PARTITION_SECTOR_SIZE],
                          uint32_t total_sectors, emes_fat_kind_t kind,
                          uint32_t hidden_sectors, const char *label,
                          emes_fat_format_layout_t *layout);
#endif
