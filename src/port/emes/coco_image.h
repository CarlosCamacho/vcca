#ifndef EMESDOS09_COCO_IMAGE_H
#define EMESDOS09_COCO_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

#define EMES_COCO_SECTOR_SIZE 256u
#define EMES_COCO_IMAGE_INFO_SIZE 32u

typedef enum {
    EMES_COCO_OK = 0,
    EMES_COCO_IO,
    EMES_COCO_INVALID,
    EMES_COCO_SEEK,
    EMES_COCO_NO_MORE
} emes_coco_result_t;

typedef enum {
    EMES_COCO_FORMAT_NONE = 0,
    EMES_COCO_FORMAT_DSK = 1,
    EMES_COCO_FORMAT_JVC = 2,
    EMES_COCO_FORMAT_SDF1 = 3
} emes_coco_format_t;

typedef bool (*emes_coco_reader_t)(uint32_t offset, uint8_t *data, uint16_t length, void *context);
typedef bool (*emes_coco_writer_t)(uint32_t offset, const uint8_t *data, uint16_t length, void *context);

typedef struct {
    emes_coco_reader_t reader;
    emes_coco_writer_t writer;
    void *context;
    uint32_t file_size;
    emes_coco_format_t format;
    uint16_t header_size;
    uint8_t cylinders;
    uint8_t sides;
    uint8_t sectors_per_track;
    uint16_t sector_size;
    uint8_t first_sector_id;
    bool read_only;
    bool nested_sectors;
    uint32_t cache_offset;
    bool cache_valid;
    uint8_t cache[512];
    uint16_t generation;
} emes_coco_image_t;

typedef struct {
    uint8_t sector_id;
    uint16_t entry_offset;
    bool finished;
} emes_decb_dir_t;

typedef struct {
    uint8_t alias_raw[11];
    uint8_t file_type;
    uint8_t ascii_flag;
    uint8_t first_granule;
    uint32_t size;
} emes_decb_entry_t;

emes_coco_result_t emes_coco_image_mount(
    emes_coco_image_t *image, emes_coco_reader_t reader, void *context,
    uint32_t file_size, uint16_t generation
);
emes_coco_result_t emes_coco_image_mount_rw(
    emes_coco_image_t *image, emes_coco_reader_t reader, emes_coco_writer_t writer,
    void *context, uint32_t file_size, uint16_t generation
);
emes_coco_result_t emes_coco_read_sector(
    emes_coco_image_t *image, uint8_t cylinder, uint8_t side,
    uint8_t sector_id, uint8_t output[EMES_COCO_SECTOR_SIZE]
);
emes_coco_result_t emes_coco_write_sector(
    emes_coco_image_t *image, uint8_t cylinder, uint8_t side,
    uint8_t sector_id, const uint8_t input[EMES_COCO_SECTOR_SIZE]
);
emes_coco_result_t emes_decb_open_dir(emes_coco_image_t *image, emes_decb_dir_t *directory);
emes_coco_result_t emes_decb_dir_next(
    emes_coco_image_t *image, emes_decb_dir_t *directory, emes_decb_entry_t *entry
);
void emes_coco_image_info(
    const emes_coco_image_t *image, const char *path, uint8_t output[EMES_COCO_IMAGE_INFO_SIZE]
);

#endif
