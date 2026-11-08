#ifndef EMESDOS09_FAT16_META_H
#define EMESDOS09_FAT16_META_H

#include <stdbool.h>
#include <stdint.h>

#include "fat16_ro.h"
#include "fat16_rw.h"

#define EMES_FAT16_ATTR_READONLY  0x01u
#define EMES_FAT16_ATTR_HIDDEN    0x02u
#define EMES_FAT16_ATTR_SYSTEM    0x04u
#define EMES_FAT16_ATTR_VOLUME    0x08u
#define EMES_FAT16_ATTR_DIRECTORY 0x10u
#define EMES_FAT16_ATTR_ARCHIVE   0x20u

typedef struct {
    uint32_t lba;
    uint16_t offset;
} emes_fat16_entry_location_t;

typedef struct {
    uint16_t time;
    uint16_t date;
} emes_fat16_dos_datetime_t;

/* Pack a validated Gregorian timestamp into DOS/FAT local date and time. */
bool emes_fat16_pack_datetime(
    uint16_t year, uint8_t month, uint8_t day,
    uint8_t hour, uint8_t minute, uint8_t second,
    emes_fat16_dos_datetime_t *out
);

/* Use 1980-01-01 00:00:00 when the hardware RTC is invalid. */
emes_fat16_dos_datetime_t emes_fat16_epoch_datetime(void);

/* Update a resolved short directory entry. LFN slots are owned by the caller. */
emes_fat16_rw_result_t emes_fat16_meta_update(
    const emes_fat16_volume_t *volume,
    emes_fat16_sector_writer_t writer,
    void *io_context,
    emes_fat16_entry_location_t location,
    uint8_t attributes,
    emes_fat16_dos_datetime_t modified
);

/* Mark a resolved short entry deleted. Associated LFN slots are supplied by
 * repeated calls from the path layer before this short entry is removed. */
emes_fat16_rw_result_t emes_fat16_meta_delete(
    const emes_fat16_volume_t *volume,
    emes_fat16_sector_writer_t writer,
    void *io_context,
    emes_fat16_entry_location_t location
);

/* Initialize the first sector of a new directory with '.' and '..'. */
emes_fat16_rw_result_t emes_fat16_meta_init_directory(
    emes_fat16_sector_writer_t writer,
    void *io_context,
    uint32_t first_lba,
    uint16_t self_cluster,
    uint16_t parent_cluster,
    emes_fat16_dos_datetime_t timestamp
);

/* Normalize an ASCII label into an 11-byte space-padded FAT label. */
bool emes_fat16_meta_label11(const char *label, uint8_t output[11]);

#endif
