#include "fat16_meta.h"

#include <stddef.h>
#include <string.h>

static void put_le16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static bool valid_day(uint16_t year, uint8_t month, uint8_t day) {
    static const uint8_t days[12] = {31u,28u,31u,30u,31u,30u,31u,31u,30u,31u,30u,31u};
    if (month == 0u || month > 12u || day == 0u) return false;
    uint8_t limit = days[month - 1u];
    const bool leap = (year % 4u == 0u) && ((year % 100u != 0u) || (year % 400u == 0u));
    if (month == 2u && leap) ++limit;
    return day <= limit;
}

bool emes_fat16_pack_datetime(
    uint16_t year, uint8_t month, uint8_t day,
    uint8_t hour, uint8_t minute, uint8_t second,
    emes_fat16_dos_datetime_t *out
) {
    if (out == NULL || year < 1980u || year > 2107u ||
        !valid_day(year, month, day) || hour > 23u || minute > 59u || second > 59u) {
        return false;
    }
    out->date = (uint16_t)(((year - 1980u) << 9) | ((uint16_t)month << 5) | day);
    out->time = (uint16_t)(((uint16_t)hour << 11) | ((uint16_t)minute << 5) | (second / 2u));
    return true;
}

emes_fat16_dos_datetime_t emes_fat16_epoch_datetime(void) {
    emes_fat16_dos_datetime_t value = {0u, 0x0021u};
    return value;
}

static emes_fat16_rw_result_t load_entry_sector(
    const emes_fat16_volume_t *volume,
    emes_fat16_entry_location_t location,
    uint8_t sector[EMES_FAT16_SECTOR_SIZE]
) {
    if (volume == NULL || location.offset > EMES_FAT16_SECTOR_SIZE - 32u ||
        !volume->read_sector(location.lba, sector, volume->context)) {
        return EMES_FAT16_RW_IO;
    }
    return EMES_FAT16_RW_OK;
}

emes_fat16_rw_result_t emes_fat16_meta_update(
    const emes_fat16_volume_t *volume,
    emes_fat16_sector_writer_t writer,
    void *io_context,
    emes_fat16_entry_location_t location,
    uint8_t attributes,
    emes_fat16_dos_datetime_t modified
) {
    uint8_t sector[EMES_FAT16_SECTOR_SIZE];
    if (writer == NULL) return EMES_FAT16_RW_INVALID;
    emes_fat16_rw_result_t result = load_entry_sector(volume, location, sector);
    if (result != EMES_FAT16_RW_OK) return result;
    uint8_t *entry = &sector[location.offset];
    if (entry[0] == 0u || entry[0] == 0xE5u || entry[11] == 0x0Fu) return EMES_FAT16_RW_INVALID;
    entry[11] = (uint8_t)(attributes & 0x3Fu);
    put_le16(&entry[22], modified.time);
    put_le16(&entry[24], modified.date);
    return writer(location.lba, sector, io_context) ? EMES_FAT16_RW_OK : EMES_FAT16_RW_IO;
}

emes_fat16_rw_result_t emes_fat16_meta_delete(
    const emes_fat16_volume_t *volume,
    emes_fat16_sector_writer_t writer,
    void *io_context,
    emes_fat16_entry_location_t location
) {
    uint8_t sector[EMES_FAT16_SECTOR_SIZE];
    if (writer == NULL) return EMES_FAT16_RW_INVALID;
    emes_fat16_rw_result_t result = load_entry_sector(volume, location, sector);
    if (result != EMES_FAT16_RW_OK) return result;
    if (sector[location.offset] == 0u || sector[location.offset] == 0xE5u) return EMES_FAT16_RW_INVALID;
    sector[location.offset] = 0xE5u;
    return writer(location.lba, sector, io_context) ? EMES_FAT16_RW_OK : EMES_FAT16_RW_IO;
}

static void make_dot_entry(
    uint8_t entry[32], bool parent, uint16_t cluster,
    emes_fat16_dos_datetime_t timestamp
) {
    memset(entry, 0, 32u);
    memset(entry, ' ', 11u);
    entry[0] = '.';
    if (parent) entry[1] = '.';
    entry[11] = EMES_FAT16_ATTR_DIRECTORY;
    put_le16(&entry[14], timestamp.time);
    put_le16(&entry[16], timestamp.date);
    put_le16(&entry[22], timestamp.time);
    put_le16(&entry[24], timestamp.date);
    put_le16(&entry[26], cluster);
}

emes_fat16_rw_result_t emes_fat16_meta_init_directory(
    emes_fat16_sector_writer_t writer,
    void *io_context,
    uint32_t first_lba,
    uint16_t self_cluster,
    uint16_t parent_cluster,
    emes_fat16_dos_datetime_t timestamp
) {
    uint8_t sector[EMES_FAT16_SECTOR_SIZE];
    if (writer == NULL || self_cluster < 2u) return EMES_FAT16_RW_INVALID;
    memset(sector, 0, sizeof(sector));
    make_dot_entry(&sector[0], false, self_cluster, timestamp);
    make_dot_entry(&sector[32], true, parent_cluster, timestamp);
    return writer(first_lba, sector, io_context) ? EMES_FAT16_RW_OK : EMES_FAT16_RW_IO;
}

bool emes_fat16_meta_label11(const char *label, uint8_t output[11]) {
    if (label == NULL || output == NULL) return false;
    memset(output, ' ', 11u);
    size_t count = 0u;
    while (*label != '\0') {
        unsigned char value = (unsigned char)*label++;
        if (value == ' ') continue;
        if (count >= 11u || value < 0x21u || value > 0x7Eu ||
            value == '"' || value == '*' || value == '+' || value == ',' ||
            value == '/' || value == ':' || value == ';' || value == '<' ||
            value == '=' || value == '>' || value == '?' || value == '[' ||
            value == '\\' || value == ']' || value == '|') return false;
        if (value >= 'a' && value <= 'z') value = (unsigned char)(value - ('a' - 'A'));
        output[count++] = (uint8_t)value;
    }
    return count != 0u;
}
