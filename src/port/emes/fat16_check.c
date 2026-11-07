#include "fat16_check.h"

#include <stddef.h>
#include <string.h>

static bool read_fat_entry(
    const emes_fat16_volume_t *volume,
    uint8_t fat_index,
    uint16_t cluster,
    uint16_t *value
) {
    uint8_t sector[EMES_FAT16_SECTOR_SIZE];
    const uint32_t offset = (uint32_t)cluster * 2u;
    const uint32_t lba = volume->fat_lba +
        (uint32_t)fat_index * volume->sectors_per_fat +
        offset / EMES_FAT16_SECTOR_SIZE;
    const uint16_t within = (uint16_t)(offset % EMES_FAT16_SECTOR_SIZE);
    if (!volume->read_sector(lba, sector, volume->context)) return false;
    if (within == EMES_FAT16_SECTOR_SIZE - 1u) {
        uint8_t next[EMES_FAT16_SECTOR_SIZE];
        if (!volume->read_sector(lba + 1u, next, volume->context)) return false;
        *value = (uint16_t)sector[within] | ((uint16_t)next[0] << 8);
    } else {
        *value = (uint16_t)sector[within] | ((uint16_t)sector[within + 1u] << 8);
    }
    return true;
}

emes_fat16_result_t emes_fat16_check_chain(
    const emes_fat16_volume_t *volume,
    uint16_t first_cluster,
    uint16_t *cluster_count,
    bool *loop_detected
) {
    if (volume == NULL || cluster_count == NULL || loop_detected == NULL) {
        return EMES_FAT16_INVALID;
    }
    *cluster_count = 0u;
    *loop_detected = false;
    if (first_cluster < 2u || first_cluster >= volume->cluster_count + 2u) {
        return EMES_FAT16_INVALID;
    }
    uint16_t cluster = first_cluster;
    /* Floyd cycle detection avoids a RAM bitmap on the small PIC32 target. */
    uint16_t slow = first_cluster;
    uint16_t fast = first_cluster;
    bool fast_active = true;
    for (;;) {
        uint16_t next;
        if (!read_fat_entry(volume, 0u, cluster, &next)) return EMES_FAT16_IO;
        ++*cluster_count;
        if (*cluster_count > volume->cluster_count) {
            *loop_detected = true;
            return EMES_FAT16_INVALID;
        }
        if (next >= 0xFFF8u) return EMES_FAT16_OK;
        if (next == 0xFFF7u || next < 2u || next >= volume->cluster_count + 2u ||
            (next >= 0xFFF0u && next <= 0xFFF6u)) {
            return EMES_FAT16_INVALID;
        }
        cluster = next;

        if (fast_active) {
            uint16_t slow_next;
            if (!read_fat_entry(volume, 0u, slow, &slow_next)) return EMES_FAT16_IO;
            slow = slow_next;
            for (uint8_t step = 0u; step < 2u; ++step) {
                uint16_t fast_next;
                if (!read_fat_entry(volume, 0u, fast, &fast_next)) return EMES_FAT16_IO;
                if (fast_next >= 0xFFF8u) {
                    fast_active = false;
                    break;
                }
                if (fast_next == 0xFFF7u || fast_next < 2u ||
                    fast_next >= volume->cluster_count + 2u) {
                    fast_active = false;
                    break;
                }
                fast = fast_next;
            }
            if (fast_active && slow == fast) {
                *loop_detected = true;
                return EMES_FAT16_INVALID;
            }
        }
    }
}

emes_fat16_result_t emes_fat16_check_volume(
    const emes_fat16_volume_t *volume,
    uint16_t media_generation,
    emes_fat16_check_report_t *report
) {
    if (volume == NULL || report == NULL || volume->read_sector == NULL) {
        return EMES_FAT16_INVALID;
    }
    memset(report, 0, sizeof(*report));
    report->version = 1u;
    report->media_generation = media_generation;
    report->total_clusters = (uint16_t)volume->cluster_count;

    for (uint32_t cluster32 = 2u; cluster32 < volume->cluster_count + 2u; ++cluster32) {
        const uint16_t cluster = (uint16_t)cluster32;
        uint16_t primary;
        if (!read_fat_entry(volume, 0u, cluster, &primary)) return EMES_FAT16_IO;
        for (uint8_t fat = 1u; fat < volume->fat_count; ++fat) {
            uint16_t copy;
            if (!read_fat_entry(volume, fat, cluster, &copy)) return EMES_FAT16_IO;
            if (copy != primary) ++report->fat_mismatches;
        }
        if (primary == 0u) {
            ++report->free_clusters;
        } else {
            ++report->used_clusters;
            if (primary == 0xFFF7u) {
                ++report->bad_clusters;
            } else if (primary < 2u ||
                       (primary > (uint16_t)(volume->cluster_count + 1u) && primary < 0xFFF8u) ||
                       (primary >= 0xFFF0u && primary <= 0xFFF6u)) {
                ++report->invalid_clusters;
            }
        }
    }

    report->errors = (uint16_t)(report->fat_mismatches + report->bad_clusters +
        report->invalid_clusters);
    if (report->free_clusters == 0u) {
        report->flags |= EMES_CHECK_FLAG_FULL;
        report->warnings = 1u;
    }
    if (report->fat_mismatches != 0u) report->flags |= EMES_CHECK_FLAG_FAT_MISMATCH;
    if (report->bad_clusters != 0u || report->invalid_clusters != 0u) {
        report->flags |= EMES_CHECK_FLAG_INVALID;
    }
    if (report->errors == 0u) report->flags |= EMES_CHECK_FLAG_CLEAN;
    return EMES_FAT16_OK;
}
