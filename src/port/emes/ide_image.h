#ifndef EMESDOS09_IDE_IMAGE_H
#define EMESDOS09_IDE_IMAGE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define EMES_IDE_SECTOR_SIZE 512u
#define EMES_IDE_HALF_SIZE 256u
#define EMES_IDE_MAX_EXTENTS 64u
typedef struct { uint32_t first_lba; uint32_t sectors; } emes_ide_extent_t;
typedef struct {
    uint8_t unit;
    bool attached;
    bool read_only;
    bool dirty;
    uint32_t sectors;
    uint16_t cylinders;
    uint8_t heads;
    uint8_t sectors_per_track;
    uint16_t extent_count;
    emes_ide_extent_t extents[EMES_IDE_MAX_EXTENTS];
} emes_ide_image_t;
typedef bool (*emes_ide_reader_t)(uint32_t offset, uint8_t *data, uint16_t length, void *context);
typedef bool (*emes_ide_writer_t)(uint32_t offset, const uint8_t *data, uint16_t length, void *context);
bool emes_ide_attach(emes_ide_image_t *ide, uint8_t unit, uint32_t byte_size, bool read_only);
bool emes_ide_read_half(const emes_ide_image_t *ide, uint32_t lba, uint8_t half, uint8_t out[EMES_IDE_HALF_SIZE], emes_ide_reader_t reader, void *context);
bool emes_ide_write_half(emes_ide_image_t *ide, uint32_t lba, uint8_t half, const uint8_t data[EMES_IDE_HALF_SIZE], emes_ide_writer_t writer, void *context);
void emes_ide_identify(const emes_ide_image_t *ide, uint8_t out[EMES_IDE_SECTOR_SIZE]);
#endif
