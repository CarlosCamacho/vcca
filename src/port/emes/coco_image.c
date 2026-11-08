#include "coco_image.h"

#include <stddef.h>
#include <string.h>

#define SDF_HEADER_SIZE 512u
#define SDF_TRACK_RECORD 6656u

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static bool read_exact(emes_coco_image_t *image, uint32_t offset, uint8_t *data, uint16_t length) {
    if (image == NULL || image->reader == NULL || data == NULL) return false;
    if (offset > image->file_size || (uint32_t)length > image->file_size - offset) return false;
    return image->reader(offset, data, length, image->context);
}

emes_coco_result_t emes_coco_image_mount(
    emes_coco_image_t *image, emes_coco_reader_t reader, void *context,
    uint32_t file_size, uint16_t generation
) {
    return emes_coco_image_mount_rw(image, reader, NULL, context, file_size, generation);
}

emes_coco_result_t emes_coco_image_mount_rw(
    emes_coco_image_t *image, emes_coco_reader_t reader, emes_coco_writer_t writer,
    void *context, uint32_t file_size, uint16_t generation
) {
    uint8_t first[8];
    if (image == NULL || reader == NULL || file_size < 8u) return EMES_COCO_INVALID;
    memset(image, 0, sizeof(*image));
    image->reader = reader;
    image->writer = writer;
    image->context = context;
    image->file_size = file_size;
    image->read_only = true;
    image->generation = generation;
    if (!read_exact(image, 0u, first, sizeof(first))) return EMES_COCO_IO;
    if (memcmp(first, "SDF1", 4u) == 0) {
        const uint8_t cylinders = first[4];
        const uint8_t sides = first[5];
        const uint8_t permission = first[6];
        const uint8_t nested = first[7];
        if (cylinders == 0u || cylinders > 80u || (sides != 1u && sides != 2u) || nested > 1u)
            return EMES_COCO_INVALID;
        const uint32_t expected = SDF_HEADER_SIZE + (uint32_t)cylinders * sides * SDF_TRACK_RECORD;
        if (file_size != expected) return EMES_COCO_INVALID;
        image->format = EMES_COCO_FORMAT_SDF1;
        image->header_size = SDF_HEADER_SIZE;
        image->cylinders = cylinders;
        image->sides = sides;
        image->sectors_per_track = 18u;
        image->sector_size = 256u;
        image->first_sector_id = 1u;
        image->nested_sectors = nested != 0u;
        image->read_only = writer == NULL || permission == 0u || nested != 0u ||
            sides != 1u || (cylinders != 35u && cylinders != 40u && cylinders != 80u);
        return EMES_COCO_OK;
    }
    const uint16_t header_size = (uint16_t)(file_size & 0xFFu);
    if (header_size > 5u) return EMES_COCO_INVALID;
    uint8_t header[5] = {18u, 1u, 1u, 1u, 0u};
    if (header_size != 0u && !read_exact(image, 0u, header, header_size)) return EMES_COCO_IO;
    const uint8_t spt = header[0];
    const uint8_t sides = header_size >= 2u ? header[1] : 1u;
    const uint8_t size_code = header_size >= 3u ? header[2] : 1u;
    const uint8_t first_sector = header_size >= 4u ? header[3] : 1u;
    const uint8_t attrs = header_size >= 5u ? header[4] : 0u;
    const uint16_t sector_size = (uint16_t)(128u << size_code);
    if (spt == 0u || (sides != 1u && sides != 2u) || sector_size != 256u || attrs != 0u)
        return EMES_COCO_INVALID;
    const uint32_t payload = file_size - header_size;
    const uint32_t track_bytes = (uint32_t)spt * sides * sector_size;
    if (payload == 0u || track_bytes == 0u || payload % track_bytes != 0u) return EMES_COCO_INVALID;
    const uint32_t cylinders = payload / track_bytes;
    if (cylinders < 18u || cylinders > 80u) return EMES_COCO_INVALID;
    image->format = header_size == 0u ? EMES_COCO_FORMAT_DSK : EMES_COCO_FORMAT_JVC;
    image->header_size = header_size;
    image->cylinders = (uint8_t)cylinders;
    image->sides = sides;
    image->sectors_per_track = spt;
    image->sector_size = sector_size;
    image->first_sector_id = first_sector;
    image->read_only = writer == NULL || sides != 1u || spt != 18u || first_sector != 1u ||
        (cylinders != 35u && cylinders != 40u && cylinders != 80u);
    return EMES_COCO_OK;
}

static emes_coco_result_t read_flat_sector(
    emes_coco_image_t *image, uint8_t cylinder, uint8_t side,
    uint8_t sector_id, uint8_t output[256]
) {
    if (sector_id < image->first_sector_id) return EMES_COCO_SEEK;
    const uint8_t index = (uint8_t)(sector_id - image->first_sector_id);
    if (index >= image->sectors_per_track) return EMES_COCO_SEEK;
    const uint32_t linear = ((uint32_t)cylinder * image->sides + side) * image->sectors_per_track + index;
    const uint32_t absolute = image->header_size + linear * 256u;
    const uint32_t pair = absolute & ~511u;
    if (!image->cache_valid || image->cache_offset != pair) {
        uint16_t available = 512u;
        if (pair + available > image->file_size) available = (uint16_t)(image->file_size - pair);
        memset(image->cache, 0, sizeof(image->cache));
        if (!read_exact(image, pair, image->cache, available)) return EMES_COCO_IO;
        image->cache_offset = pair;
        image->cache_valid = true;
    }
    memcpy(output, &image->cache[absolute - pair], 256u);
    return EMES_COCO_OK;
}

static emes_coco_result_t read_sdf_sector(
    emes_coco_image_t *image, uint8_t cylinder, uint8_t side,
    uint8_t sector_id, uint8_t output[256]
) {
    uint8_t header[256];
    const uint32_t record = SDF_HEADER_SIZE + ((uint32_t)cylinder * image->sides + side) * SDF_TRACK_RECORD;
    if (!read_exact(image, record, header, sizeof(header))) return EMES_COCO_IO;
    const uint8_t count = header[0];
    if (count > 31u) return EMES_COCO_INVALID;
    for (uint8_t i = 0u; i < count; ++i) {
        const uint8_t *entry = &header[8u + (uint16_t)i * 8u];
        const uint16_t data_offset = (uint16_t)(le16(&entry[2]) & 0x3FFFu);
        const uint8_t size_code = entry[7];
        if (entry[4] == cylinder && entry[5] == side && entry[6] == sector_id) {
            if (size_code != 1u || data_offset < 256u || data_offset > SDF_TRACK_RECORD - 256u)
                return EMES_COCO_INVALID;
            return read_exact(image, record + data_offset, output, 256u) ? EMES_COCO_OK : EMES_COCO_IO;
        }
    }
    return EMES_COCO_SEEK;
}

emes_coco_result_t emes_coco_read_sector(
    emes_coco_image_t *image, uint8_t cylinder, uint8_t side,
    uint8_t sector_id, uint8_t output[256]
) {
    if (image == NULL || output == NULL || image->format == EMES_COCO_FORMAT_NONE)
        return EMES_COCO_INVALID;
    if (cylinder >= image->cylinders || side >= image->sides) return EMES_COCO_SEEK;
    return image->format == EMES_COCO_FORMAT_SDF1
        ? read_sdf_sector(image, cylinder, side, sector_id, output)
        : read_flat_sector(image, cylinder, side, sector_id, output);
}

static emes_coco_result_t flat_sector_offset(
    const emes_coco_image_t *image, uint8_t cylinder, uint8_t side, uint8_t sector_id, uint32_t *offset
) {
    if (image == NULL || offset == NULL || cylinder >= image->cylinders || side >= image->sides)
        return EMES_COCO_SEEK;
    if (sector_id < image->first_sector_id) return EMES_COCO_SEEK;
    const uint8_t index = (uint8_t)(sector_id - image->first_sector_id);
    if (index >= image->sectors_per_track) return EMES_COCO_SEEK;
    const uint32_t linear = ((uint32_t)cylinder * image->sides + side) * image->sectors_per_track + index;
    *offset = image->header_size + linear * 256u;
    return *offset + 256u <= image->file_size ? EMES_COCO_OK : EMES_COCO_SEEK;
}

static emes_coco_result_t sdf_sector_offset(
    emes_coco_image_t *image, uint8_t cylinder, uint8_t side, uint8_t sector_id, uint32_t *offset
) {
    uint8_t header[256];
    const uint32_t record = SDF_HEADER_SIZE + ((uint32_t)cylinder * image->sides + side) * SDF_TRACK_RECORD;
    if (!read_exact(image, record, header, sizeof(header))) return EMES_COCO_IO;
    if (header[0] > 31u) return EMES_COCO_INVALID;
    for (uint8_t i = 0u; i < header[0]; ++i) {
        const uint8_t *entry = &header[8u + (uint16_t)i * 8u];
        const uint16_t data_offset = (uint16_t)(le16(&entry[2]) & 0x3FFFu);
        if (entry[4] == cylinder && entry[5] == side && entry[6] == sector_id) {
            if (entry[7] != 1u || data_offset < 256u || data_offset > SDF_TRACK_RECORD - 256u)
                return EMES_COCO_INVALID;
            *offset = record + data_offset;
            return EMES_COCO_OK;
        }
    }
    return EMES_COCO_SEEK;
}

emes_coco_result_t emes_coco_write_sector(
    emes_coco_image_t *image, uint8_t cylinder, uint8_t side,
    uint8_t sector_id, const uint8_t input[256]
) {
    uint32_t offset = 0u;
    if (image == NULL || input == NULL || image->format == EMES_COCO_FORMAT_NONE) return EMES_COCO_INVALID;
    if (image->read_only || image->writer == NULL) return EMES_COCO_IO;
    emes_coco_result_t result = image->format == EMES_COCO_FORMAT_SDF1
        ? sdf_sector_offset(image, cylinder, side, sector_id, &offset)
        : flat_sector_offset(image, cylinder, side, sector_id, &offset);
    if (result != EMES_COCO_OK) return result;
    if (!image->writer(offset, input, 256u, image->context)) return EMES_COCO_IO;
    image->cache_valid = false;
    return EMES_COCO_OK;
}

emes_coco_result_t emes_decb_open_dir(emes_coco_image_t *image, emes_decb_dir_t *directory) {
    uint8_t sector[256];
    if (image == NULL || directory == NULL || image->cylinders <= 17u) return EMES_COCO_INVALID;
    if (emes_coco_read_sector(image, 17u, 0u, 2u, sector) != EMES_COCO_OK) return EMES_COCO_INVALID;
    directory->sector_id = 3u;
    directory->entry_offset = 0u;
    directory->finished = false;
    return EMES_COCO_OK;
}

static emes_coco_result_t decb_size(
    emes_coco_image_t *image, uint8_t first_granule, uint16_t last_bytes, uint32_t *size
) {
    uint8_t fat[256];
    uint8_t seen[9] = {0};
    uint8_t granule = first_granule;
    uint32_t total = 0u;
    if (size == NULL || emes_coco_read_sector(image, 17u, 0u, 2u, fat) != EMES_COCO_OK)
        return EMES_COCO_INVALID;
    if (granule >= 68u) { *size = 0u; return EMES_COCO_OK; }
    for (uint8_t steps = 0u; steps < 68u; ++steps) {
        const uint8_t bit = (uint8_t)(1u << (granule & 7u));
        if ((seen[granule >> 3] & bit) != 0u) return EMES_COCO_INVALID;
        seen[granule >> 3] |= bit;
        const uint8_t marker = fat[granule];
        if (marker < 68u) { total += 2304u; granule = marker; continue; }
        if (marker >= 0xC0u && marker <= 0xC9u) {
            const uint8_t sectors = (uint8_t)(marker & 0x3Fu);
            if (sectors != 0u) total += (uint32_t)(sectors - 1u) * 256u + (last_bytes == 0u ? 256u : last_bytes);
            *size = total;
            return EMES_COCO_OK;
        }
        return EMES_COCO_INVALID;
    }
    return EMES_COCO_INVALID;
}

emes_coco_result_t emes_decb_dir_next(
    emes_coco_image_t *image, emes_decb_dir_t *directory, emes_decb_entry_t *entry
) {
    uint8_t sector[256];
    if (image == NULL || directory == NULL || entry == NULL || directory->finished)
        return EMES_COCO_NO_MORE;
    while (directory->sector_id <= 11u) {
        emes_coco_result_t result = emes_coco_read_sector(image, 17u, 0u, directory->sector_id, sector);
        if (result != EMES_COCO_OK) return result;
        while (directory->entry_offset < 256u) {
            const uint8_t *raw = &sector[directory->entry_offset];
            directory->entry_offset = (uint16_t)(directory->entry_offset + 32u);
            if (raw[0] == 0xFFu) { directory->finished = true; return EMES_COCO_NO_MORE; }
            if (raw[0] == 0x00u) continue;
            for (uint8_t i = 0u; i < 11u; ++i) {
                if (raw[i] < 0x20u || raw[i] > 0x7Eu) return EMES_COCO_INVALID;
                entry->alias_raw[i] = raw[i];
            }
            entry->file_type = raw[11];
            entry->ascii_flag = raw[12];
            entry->first_granule = raw[13];
            const uint16_t last = (uint16_t)(((uint16_t)raw[14] << 8) | raw[15]);
            return decb_size(image, entry->first_granule, last, &entry->size);
        }
        ++directory->sector_id;
        directory->entry_offset = 0u;
    }
    directory->finished = true;
    return EMES_COCO_NO_MORE;
}

void emes_coco_image_info(
    const emes_coco_image_t *image, const char *path, uint8_t output[32]
) {
    if (output == NULL) return;
    memset(output, 0, 32u);
    if (image == NULL) return;
    output[0] = 1u;
    output[1] = (uint8_t)image->format;
    output[2] = image->cylinders;
    output[3] = image->sides;
    output[4] = image->sectors_per_track;
    output[5] = image->read_only ? 1u : 0u;
    output[6] = (uint8_t)(image->sector_size >> 8);
    output[7] = (uint8_t)image->sector_size;
    output[8] = (uint8_t)(image->generation >> 8);
    output[9] = (uint8_t)image->generation;
    if (path != NULL) {
        const char *base = path;
        for (const char *p = path; *p != '\0'; ++p) if (*p == '\\' || *p == '/') base = p + 1;
        for (uint8_t i = 0u; i < 21u && base[i] != '\0'; ++i) output[10u + i] = (uint8_t)base[i];
    }
}
