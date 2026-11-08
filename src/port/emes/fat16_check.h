#ifndef EMESDOS09_FAT16_CHECK_H
#define EMESDOS09_FAT16_CHECK_H

#include <stdbool.h>
#include <stdint.h>

#include "fat16_ro.h"

#define EMES_CHECK_FLAG_CLEAN        0x01u
#define EMES_CHECK_FLAG_FULL         0x02u
#define EMES_CHECK_FLAG_FAT_MISMATCH 0x04u
#define EMES_CHECK_FLAG_LOOP         0x08u
#define EMES_CHECK_FLAG_INVALID      0x10u
#define EMES_CHECK_FLAG_LOST         0x20u
#define EMES_CHECK_FLAG_CROSSLINK    0x40u
#define EMES_CHECK_FLAG_TRUNCATED    0x80u

typedef struct {
    uint8_t flags;
    uint8_t version;
    uint16_t errors;
    uint16_t warnings;
    uint16_t files;
    uint16_t directories;
    uint16_t used_clusters;
    uint16_t free_clusters;
    uint16_t lost_clusters;
    uint16_t crosslinked_clusters;
    uint16_t loop_chains;
    uint16_t invalid_clusters;
    uint16_t bad_clusters;
    uint16_t fat_mismatches;
    uint16_t truncated_files;
    uint16_t total_clusters;
    uint16_t media_generation;
} emes_fat16_check_report_t;

/* Read-only first-pass integrity scan. This portable layer compares every FAT
 * copy and validates every FAT entry. The controller path layer supplies
 * reachable-chain ownership counts as it enumerates directories. */
emes_fat16_result_t emes_fat16_check_volume(
    const emes_fat16_volume_t *volume,
    uint16_t media_generation,
    emes_fat16_check_report_t *report
);

/* Validate one chain while detecting loops, invalid transitions, and bad
 * clusters. The caller may use this during recursive directory traversal. */
emes_fat16_result_t emes_fat16_check_chain(
    const emes_fat16_volume_t *volume,
    uint16_t first_cluster,
    uint16_t *cluster_count,
    bool *loop_detected
);

#endif
