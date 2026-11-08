#include "fat16_ro.h"

#include <string.h>

#define FAT16_MIN_CLUSTERS 4085u
#define FAT16_MAX_CLUSTERS 65524u
#define FLAG_MOUNTED 0x01u
#define FLAG_MBR 0x02u
#define FLAG_SUPERFLOPPY 0x04u
#define FLAG_LFN 0x08u
#define ATTR_VOLUME 0x08u
#define ATTR_DIRECTORY 0x10u
#define ATTR_LFN 0x0Fu

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool power_of_two(uint8_t value) {
    return value != 0u && (value & (uint8_t)(value - 1u)) == 0u;
}

static bool bpb_valid(const uint8_t sector[EMES_FAT16_SECTOR_SIZE]) {
    return sector[510] == 0x55u && sector[511] == 0xAAu &&
        le16(&sector[11]) == EMES_FAT16_SECTOR_SIZE &&
        power_of_two(sector[13]) && sector[16] != 0u &&
        le16(&sector[17]) != 0u && le16(&sector[22]) != 0u;
}

static bool fat16_partition(uint8_t type) {
    return type == 0x04u || type == 0x06u || type == 0x0Eu;
}

static emes_fat16_result_t read_sector(
    const emes_fat16_volume_t *volume,
    uint32_t lba,
    uint8_t sector[EMES_FAT16_SECTOR_SIZE]
) {
    if (volume == NULL || volume->read_sector == NULL) return EMES_FAT16_INVALID;
    return volume->read_sector(lba, sector, volume->context) ? EMES_FAT16_OK : EMES_FAT16_IO;
}

static emes_fat16_result_t fat_entry(
    const emes_fat16_volume_t *volume,
    uint16_t cluster,
    uint16_t *next
) {
    uint8_t sector[EMES_FAT16_SECTOR_SIZE];
    const uint32_t offset = (uint32_t)cluster * 2u;
    const uint32_t lba = volume->fat_lba + offset / EMES_FAT16_SECTOR_SIZE;
    const uint16_t within = (uint16_t)(offset % EMES_FAT16_SECTOR_SIZE);
    emes_fat16_result_t result = read_sector(volume, lba, sector);
    if (result != EMES_FAT16_OK) return result;
    if (within == EMES_FAT16_SECTOR_SIZE - 1u) {
        uint8_t second[EMES_FAT16_SECTOR_SIZE];
        result = read_sector(volume, lba + 1u, second);
        if (result != EMES_FAT16_OK) return result;
        *next = (uint16_t)sector[within] | ((uint16_t)second[0] << 8);
    } else {
        *next = le16(&sector[within]);
    }
    return EMES_FAT16_OK;
}

static char upper_ascii(char value) {
    if (value >= 'a' && value <= 'z') return (char)(value - ('a' - 'A'));
    return value;
}

static bool is_separator(char value) {
    return value == '\\' || value == '/';
}

static bool is_illegal(char value) {
    static const char illegal[] = "\"/\\[]:;=,+<>|";
    if ((unsigned char)value < 32u || (unsigned char)value >= 127u) return true;
    for (size_t i = 0u; illegal[i] != '\0'; ++i) if (value == illegal[i]) return true;
    return false;
}

static emes_fat16_result_t compile_field(
    const char *text,
    size_t length,
    uint8_t *output,
    size_t width
) {
    size_t out = 0u;
    for (size_t i = 0u; i < length; ++i) {
        char ch = upper_ascii(text[i]);
        if (ch == '*') {
            while (out < width) output[out++] = '?';
            return i + 1u == length ? EMES_FAT16_OK : EMES_FAT16_PATH_NOT_FOUND;
        }
        if (out >= width || is_illegal(ch)) return EMES_FAT16_PATH_NOT_FOUND;
        output[out++] = (uint8_t)ch;
    }
    while (out < width) output[out++] = ' ';
    return EMES_FAT16_OK;
}

static emes_fat16_result_t compile_pattern(const char *pattern, uint8_t output[11]) {
    const char *dot = NULL;
    size_t length = strlen(pattern);
    if (length == 0u) pattern = "*.*";
    length = strlen(pattern);
    for (size_t i = 0u; i < length; ++i) {
        if (pattern[i] == '.') {
            if (dot != NULL) return EMES_FAT16_PATH_NOT_FOUND;
            dot = &pattern[i];
        }
    }
    const size_t base_len = dot == NULL ? length : (size_t)(dot - pattern);
    emes_fat16_result_t result = compile_field(pattern, base_len, output, 8u);
    if (result != EMES_FAT16_OK) return result;
    if (dot != NULL) {
        return compile_field(dot + 1, length - base_len - 1u, &output[8], 3u);
    }
    if (memchr(pattern, '*', length) != NULL) {
        output[8] = '?'; output[9] = '?'; output[10] = '?';
    } else {
        output[8] = ' '; output[9] = ' '; output[10] = ' ';
    }
    return EMES_FAT16_OK;
}

static bool pattern_matches(const uint8_t alias[11], const uint8_t pattern[11]) {
    for (size_t i = 0u; i < 11u; ++i) {
        if (pattern[i] != '?' && alias[i] != pattern[i]) return false;
    }
    return true;
}

static emes_fat16_result_t entry_at(
    const emes_fat16_volume_t *volume,
    bool root,
    uint16_t first_cluster,
    uint32_t index,
    uint8_t raw[32]
) {
    uint8_t sector[EMES_FAT16_SECTOR_SIZE];
    const uint32_t byte_offset = index * 32u;
    uint32_t lba;
    uint16_t within;
    if (root) {
        if (index >= volume->root_entries) return EMES_FAT16_NO_MORE;
        lba = volume->root_lba + byte_offset / EMES_FAT16_SECTOR_SIZE;
        within = (uint16_t)(byte_offset % EMES_FAT16_SECTOR_SIZE);
    } else {
        const uint32_t cluster_bytes = (uint32_t)volume->sectors_per_cluster * EMES_FAT16_SECTOR_SIZE;
        uint32_t skip = byte_offset / cluster_bytes;
        uint32_t in_chain = byte_offset % cluster_bytes;
        uint16_t cluster = first_cluster;
        for (uint32_t i = 0u; i < skip; ++i) {
            uint16_t next = 0u;
            emes_fat16_result_t result = fat_entry(volume, cluster, &next);
            if (result != EMES_FAT16_OK) return result;
            if (next >= 0xFFF8u) return EMES_FAT16_NO_MORE;
            if (next < 2u || next == 0xFFF7u) return EMES_FAT16_INVALID;
            cluster = next;
        }
        if (cluster < 2u || cluster >= volume->cluster_count + 2u) return EMES_FAT16_INVALID;
        lba = volume->data_lba + (uint32_t)(cluster - 2u) * volume->sectors_per_cluster +
            in_chain / EMES_FAT16_SECTOR_SIZE;
        within = (uint16_t)(in_chain % EMES_FAT16_SECTOR_SIZE);
    }
    emes_fat16_result_t result = read_sector(volume, lba, sector);
    if (result != EMES_FAT16_OK) return result;
    memcpy(raw, &sector[within], 32u);
    return EMES_FAT16_OK;
}

static void decode_entry(const uint8_t raw[32], bool lfn, emes_fat16_entry_t *entry) {
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->alias_raw, raw, 11u);
    entry->attributes = raw[11];
    entry->flags = (uint8_t)((lfn ? 0x01u : 0u) | ((raw[11] & ATTR_DIRECTORY) ? 0x02u : 0u));
    entry->create_time = le16(&raw[14]);
    entry->create_date = le16(&raw[16]);
    entry->write_time = le16(&raw[22]);
    entry->write_date = le16(&raw[24]);
    entry->first_cluster = le16(&raw[26]);
    entry->size = le32(&raw[28]);
}

static bool is_dot_entry(const uint8_t alias[11]) {
    return alias[0] == '.' && (alias[1] == ' ' || alias[1] == '.');
}

static emes_fat16_result_t find_exact(
    const emes_fat16_volume_t *volume,
    bool root,
    uint16_t cluster,
    const uint8_t wanted[11],
    emes_fat16_entry_t *entry
) {
    emes_fat16_dir_t dir;
    memset(&dir, 0, sizeof(dir));
    dir.root = root;
    dir.first_cluster = cluster;
    memset(dir.pattern, '?', sizeof(dir.pattern));
    while (true) {
        emes_fat16_result_t result = emes_fat16_dir_next(volume, &dir, entry);
        if (result != EMES_FAT16_OK) return result == EMES_FAT16_NO_MORE ? EMES_FAT16_NOT_FOUND : result;
        if (memcmp(entry->alias_raw, wanted, 11u) == 0) return EMES_FAT16_OK;
    }
}

static emes_fat16_result_t component_alias(const char *text, size_t length, uint8_t alias[11]) {
    if (length == 0u || length > 12u) return EMES_FAT16_PATH_NOT_FOUND;
    char temp[13];
    memcpy(temp, text, length);
    temp[length] = '\0';
    if (memchr(temp, '*', length) != NULL || memchr(temp, '?', length) != NULL) return EMES_FAT16_PATH_NOT_FOUND;
    return compile_pattern(temp, alias);
}

static emes_fat16_result_t resolve_parent(
    const emes_fat16_volume_t *volume,
    const char *path,
    const char **final_component,
    bool *root,
    uint16_t *cluster
) {
    const char *cursor = path;
    if (cursor[0] != '\0' && cursor[1] == ':') {
        if (upper_ascii(cursor[0]) != (char)('A' + volume->flags * 0u)) {
            /* Only drive A is mounted by the current protocol 1.x model. */
            if (upper_ascii(cursor[0]) != 'A') return EMES_FAT16_PATH_NOT_FOUND;
        }
        cursor += 2;
    }
    while (is_separator(*cursor)) ++cursor;
    *root = true;
    *cluster = 0u;
    const char *component = cursor;
    while (*cursor != '\0') {
        while (*cursor != '\0' && !is_separator(*cursor)) ++cursor;
        const char *next = cursor;
        while (is_separator(*next)) ++next;
        if (*next == '\0') {
            *final_component = component;
            return EMES_FAT16_OK;
        }
        const size_t length = (size_t)(cursor - component);
        if (length == 1u && component[0] == '.') {
            component = next;
            cursor = next;
            continue;
        }
        if (length == 2u && component[0] == '.' && component[1] == '.') return EMES_FAT16_PATH_NOT_FOUND;
        uint8_t alias[11];
        emes_fat16_result_t result = component_alias(component, length, alias);
        if (result != EMES_FAT16_OK) return result;
        emes_fat16_entry_t entry;
        result = find_exact(volume, *root, *cluster, alias, &entry);
        if (result != EMES_FAT16_OK || (entry.attributes & ATTR_DIRECTORY) == 0u) return EMES_FAT16_PATH_NOT_FOUND;
        *root = false;
        *cluster = entry.first_cluster;
        component = next;
        cursor = next;
    }
    *final_component = cursor;
    return EMES_FAT16_OK;
}

emes_fat16_result_t emes_fat16_mount(
    emes_fat16_volume_t *volume,
    emes_fat16_sector_reader_t reader,
    void *context
) {
    uint8_t sector[EMES_FAT16_SECTOR_SIZE];
    uint32_t partition_lba = 0u;
    uint8_t layout = FLAG_SUPERFLOPPY;
    if (volume == NULL || reader == NULL) return EMES_FAT16_INVALID;
    memset(volume, 0, sizeof(*volume));
    volume->read_sector = reader;
    volume->context = context;
    if (!reader(0u, sector, context)) return EMES_FAT16_IO;
    if (!bpb_valid(sector)) {
        bool found = false;
        if (sector[510] != 0x55u || sector[511] != 0xAAu) return EMES_FAT16_INVALID;
        for (uint8_t i = 0u; i < 4u; ++i) {
            const uint16_t off = (uint16_t)(446u + (uint16_t)i * 16u);
            const uint8_t type = sector[off + 4u];
            const uint32_t start = le32(&sector[off + 8u]);
            const uint32_t count = le32(&sector[off + 12u]);
            if (!fat16_partition(type) || count == 0u) continue;
            if (!reader(start, sector, context)) return EMES_FAT16_IO;
            if (bpb_valid(sector)) {
                partition_lba = start;
                layout = FLAG_MBR;
                found = true;
                break;
            }
        }
        if (!found) return EMES_FAT16_INVALID;
    }
    volume->partition_lba = partition_lba;
    volume->bytes_per_sector = le16(&sector[11]);
    volume->sectors_per_cluster = sector[13];
    volume->reserved_sectors = le16(&sector[14]);
    volume->fat_count = sector[16];
    volume->root_entries = le16(&sector[17]);
    volume->total_sectors = le16(&sector[19]);
    if (volume->total_sectors == 0u) volume->total_sectors = le32(&sector[32]);
    volume->sectors_per_fat = le16(&sector[22]);
    volume->volume_serial = le32(&sector[39]);
    memcpy(volume->volume_label, &sector[43], 11u);
    volume->volume_label[11] = '\0';
    if (volume->bytes_per_sector != EMES_FAT16_SECTOR_SIZE ||
        !power_of_two(volume->sectors_per_cluster) || volume->reserved_sectors == 0u ||
        volume->fat_count == 0u || volume->root_entries == 0u ||
        volume->sectors_per_fat == 0u || volume->total_sectors == 0u) return EMES_FAT16_INVALID;
    volume->root_dir_sectors = (uint16_t)((((uint32_t)volume->root_entries * 32u) + 511u) / 512u);
    const uint32_t first_data = (uint32_t)volume->reserved_sectors +
        (uint32_t)volume->fat_count * volume->sectors_per_fat + volume->root_dir_sectors;
    if (first_data >= volume->total_sectors) return EMES_FAT16_INVALID;
    volume->cluster_count = (volume->total_sectors - first_data) / volume->sectors_per_cluster;
    if (volume->cluster_count < FAT16_MIN_CLUSTERS || volume->cluster_count > FAT16_MAX_CLUSTERS) return EMES_FAT16_INVALID;
    volume->fat_lba = partition_lba + volume->reserved_sectors;
    volume->root_lba = volume->fat_lba + (uint32_t)volume->fat_count * volume->sectors_per_fat;
    volume->data_lba = volume->root_lba + volume->root_dir_sectors;
    volume->flags = (uint8_t)(FLAG_MOUNTED | layout);
    emes_fat16_dir_t dir;
    memset(&dir, 0, sizeof(dir));
    dir.root = true;
    memset(dir.pattern, '?', sizeof(dir.pattern));
    emes_fat16_entry_t entry;
    while (emes_fat16_dir_next(volume, &dir, &entry) == EMES_FAT16_OK) {
        if ((entry.flags & 0x01u) != 0u) { volume->flags |= FLAG_LFN; break; }
    }
    return EMES_FAT16_OK;
}

emes_fat16_result_t emes_fat16_open_dir(
    const emes_fat16_volume_t *volume,
    const char *path_pattern,
    emes_fat16_dir_t *directory
) {
    if (volume == NULL || path_pattern == NULL || directory == NULL) return EMES_FAT16_INVALID;
    const char *final = NULL;
    bool root = true;
    uint16_t cluster = 0u;
    emes_fat16_result_t result = resolve_parent(volume, path_pattern, &final, &root, &cluster);
    if (result != EMES_FAT16_OK) return result;
    memset(directory, 0, sizeof(*directory));
    directory->root = root;
    directory->first_cluster = cluster;
    return compile_pattern(*final == '\0' ? "*.*" : final, directory->pattern);
}

emes_fat16_result_t emes_fat16_dir_next(
    const emes_fat16_volume_t *volume,
    emes_fat16_dir_t *directory,
    emes_fat16_entry_t *entry
) {
    uint8_t raw[32];
    if (volume == NULL || directory == NULL || entry == NULL) return EMES_FAT16_INVALID;
    while (true) {
        emes_fat16_result_t result = entry_at(
            volume, directory->root, directory->first_cluster, directory->next_index++, raw
        );
        if (result != EMES_FAT16_OK) return result;
        if (raw[0] == 0x00u) return EMES_FAT16_NO_MORE;
        if (raw[0] == 0xE5u) { directory->pending_lfn = false; continue; }
        if (raw[11] == ATTR_LFN) { directory->pending_lfn = true; continue; }
        const bool lfn = directory->pending_lfn;
        directory->pending_lfn = false;
        if ((raw[11] & ATTR_VOLUME) != 0u || is_dot_entry(raw)) continue;
        if (!pattern_matches(raw, directory->pattern)) continue;
        decode_entry(raw, lfn, entry);
        return EMES_FAT16_OK;
    }
}

emes_fat16_result_t emes_fat16_open_path(
    const emes_fat16_volume_t *volume,
    const char *path,
    emes_fat16_file_t *file
) {
    if (volume == NULL || path == NULL || file == NULL) return EMES_FAT16_INVALID;
    const char *final = NULL;
    bool root = true;
    uint16_t cluster = 0u;
    emes_fat16_result_t result = resolve_parent(volume, path, &final, &root, &cluster);
    if (result != EMES_FAT16_OK || *final == '\0') return EMES_FAT16_PATH_NOT_FOUND;
    const char *end = final;
    while (*end != '\0' && !is_separator(*end)) ++end;
    uint8_t alias[11];
    result = component_alias(final, (size_t)(end - final), alias);
    if (result != EMES_FAT16_OK) return result;
    result = find_exact(volume, root, cluster, alias, &file->entry);
    if (result != EMES_FAT16_OK) return result;
    if ((file->entry.attributes & ATTR_DIRECTORY) != 0u) return EMES_FAT16_PATH_NOT_FOUND;
    file->position = 0u;
    return EMES_FAT16_OK;
}

emes_fat16_result_t emes_fat16_read(
    const emes_fat16_volume_t *volume,
    emes_fat16_file_t *file,
    uint8_t *destination,
    uint16_t requested,
    uint16_t *actual
) {
    uint8_t sector[EMES_FAT16_SECTOR_SIZE];
    if (volume == NULL || file == NULL || destination == NULL || actual == NULL) return EMES_FAT16_INVALID;
    *actual = 0u;
    if (file->position >= file->entry.size || requested == 0u) return EMES_FAT16_OK;
    uint32_t remaining = file->entry.size - file->position;
    if (remaining > requested) remaining = requested;
    const uint32_t cluster_bytes = (uint32_t)volume->sectors_per_cluster * EMES_FAT16_SECTOR_SIZE;
    uint32_t cluster_index = file->position / cluster_bytes;
    uint32_t in_cluster = file->position % cluster_bytes;
    uint16_t cluster = file->entry.first_cluster;
    for (uint32_t i = 0u; i < cluster_index; ++i) {
        uint16_t next = 0u;
        emes_fat16_result_t result = fat_entry(volume, cluster, &next);
        if (result != EMES_FAT16_OK) return result;
        if (next < 2u || next >= 0xFFF8u) return EMES_FAT16_INVALID;
        cluster = next;
    }
    while (remaining > 0u) {
        if (cluster < 2u || cluster >= volume->cluster_count + 2u) return EMES_FAT16_INVALID;
        const uint32_t sector_index = in_cluster / EMES_FAT16_SECTOR_SIZE;
        const uint16_t within = (uint16_t)(in_cluster % EMES_FAT16_SECTOR_SIZE);
        const uint32_t lba = volume->data_lba + (uint32_t)(cluster - 2u) * volume->sectors_per_cluster + sector_index;
        emes_fat16_result_t result = read_sector(volume, lba, sector);
        if (result != EMES_FAT16_OK) return result;
        uint32_t take = EMES_FAT16_SECTOR_SIZE - within;
        if (take > remaining) take = remaining;
        memcpy(&destination[*actual], &sector[within], take);
        *actual = (uint16_t)(*actual + take);
        file->position += take;
        remaining -= take;
        in_cluster += take;
        if (remaining > 0u && in_cluster >= cluster_bytes) {
            uint16_t next = 0u;
            result = fat_entry(volume, cluster, &next);
            if (result != EMES_FAT16_OK) return result;
            if (next < 2u || next >= 0xFFF8u) return EMES_FAT16_INVALID;
            cluster = next;
            in_cluster = 0u;
        }
    }
    return EMES_FAT16_OK;
}

emes_fat16_result_t emes_fat16_seek(emes_fat16_file_t *file, uint32_t offset, uint8_t whence) {
    if (file == NULL) return EMES_FAT16_INVALID;
    uint32_t target;
    if (whence == 0u) target = offset;
    else if (whence == 1u) {
        if (UINT32_MAX - file->position < offset) return EMES_FAT16_SEEK;
        target = file->position + offset;
    } else if (whence == 2u) {
        if (UINT32_MAX - file->entry.size < offset) return EMES_FAT16_SEEK;
        target = file->entry.size + offset;
    } else return EMES_FAT16_SEEK;
    if (target > file->entry.size) return EMES_FAT16_SEEK;
    file->position = target;
    return EMES_FAT16_OK;
}

emes_fat16_result_t emes_fat16_free_space(const emes_fat16_volume_t *volume, emes_fat16_free_t *info) {
    if (volume == NULL || info == NULL) return EMES_FAT16_INVALID;
    uint32_t free_count = 0u;
    for (uint32_t cluster = 2u; cluster < volume->cluster_count + 2u; ++cluster) {
        uint16_t value = 0u;
        emes_fat16_result_t result = fat_entry(volume, (uint16_t)cluster, &value);
        if (result != EMES_FAT16_OK) return result;
        if (value == 0u) ++free_count;
    }
    info->bytes_per_sector = volume->bytes_per_sector;
    info->sectors_per_cluster = volume->sectors_per_cluster;
    info->free_clusters = (uint16_t)free_count;
    info->total_clusters = (uint16_t)volume->cluster_count;
    info->total_sectors = volume->total_sectors;
    return EMES_FAT16_OK;
}

emes_fat16_result_t emes_fat16_root_next(
    const emes_fat16_volume_t *volume,
    emes_fat16_dir_t *directory,
    emes_fat16_entry_t *entry
) {
    if (directory != NULL && directory->next_index == 0u) {
        directory->root = true;
        memset(directory->pattern, '?', sizeof(directory->pattern));
    }
    return emes_fat16_dir_next(volume, directory, entry);
}

emes_fat16_result_t emes_fat16_open_root(
    const emes_fat16_volume_t *volume,
    const char *path,
    emes_fat16_file_t *file
) {
    return emes_fat16_open_path(volume, path, file);
}
