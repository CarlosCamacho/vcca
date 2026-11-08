#include "fat16_rw.h"

#include <string.h>

static uint16_t le16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void put_le16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void put_le32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static bool boundary(
    emes_fat16_commit_boundary_t value,
    emes_fat16_boundary_hook_t hook,
    void *context,
    emes_fat16_commit_result_t *result
) {
    result->last_boundary = value;
    return hook == NULL || hook(value, context);
}

static emes_fat16_rw_result_t read_fat_entry(
    const emes_fat16_volume_t *volume,
    uint16_t cluster,
    uint16_t *value
) {
    uint8_t sector[EMES_FAT16_SECTOR_SIZE];
    const uint32_t byte_offset = (uint32_t)cluster * 2u;
    const uint32_t lba = volume->fat_lba + byte_offset / EMES_FAT16_SECTOR_SIZE;
    const uint16_t offset = (uint16_t)(byte_offset % EMES_FAT16_SECTOR_SIZE);
    if (!volume->read_sector(lba, sector, volume->context)) return EMES_FAT16_RW_IO;
    *value = le16(&sector[offset]);
    return EMES_FAT16_RW_OK;
}

static emes_fat16_rw_result_t write_fat_entry(
    const emes_fat16_volume_t *volume,
    emes_fat16_sector_writer_t writer,
    void *context,
    uint8_t fat_copy,
    uint16_t cluster,
    uint16_t value
) {
    uint8_t sector[EMES_FAT16_SECTOR_SIZE];
    const uint32_t byte_offset = (uint32_t)cluster * 2u;
    const uint32_t lba = volume->fat_lba
        + (uint32_t)fat_copy * volume->sectors_per_fat
        + byte_offset / EMES_FAT16_SECTOR_SIZE;
    const uint16_t offset = (uint16_t)(byte_offset % EMES_FAT16_SECTOR_SIZE);
    if (!volume->read_sector(lba, sector, volume->context)) return EMES_FAT16_RW_IO;
    put_le16(&sector[offset], value);
    return writer(lba, sector, context) ? EMES_FAT16_RW_OK : EMES_FAT16_RW_IO;
}

static emes_fat16_rw_result_t allocate_clusters(
    const emes_fat16_volume_t *volume,
    uint16_t needed,
    uint16_t *clusters
) {
    uint16_t found = 0u;
    for (uint32_t cluster = 2u; cluster < volume->cluster_count + 2u && found < needed; ++cluster) {
        uint16_t value = 0u;
        emes_fat16_rw_result_t status = read_fat_entry(volume, (uint16_t)cluster, &value);
        if (status != EMES_FAT16_RW_OK) return status;
        if (value == 0u) clusters[found++] = (uint16_t)cluster;
    }
    return found == needed ? EMES_FAT16_RW_OK : EMES_FAT16_RW_NO_SPACE;
}

static emes_fat16_rw_result_t write_data_clusters(
    const emes_fat16_volume_t *volume,
    emes_fat16_sector_writer_t writer,
    void *context,
    const uint16_t *clusters,
    uint16_t cluster_count,
    const uint8_t *data,
    uint32_t size,
    emes_fat16_commit_result_t *result
) {
    uint8_t sector[EMES_FAT16_SECTOR_SIZE];
    uint32_t position = 0u;
    for (uint16_t index = 0u; index < cluster_count; ++index) {
        const uint32_t first_lba = volume->data_lba
            + ((uint32_t)clusters[index] - 2u) * volume->sectors_per_cluster;
        for (uint8_t sector_index = 0u; sector_index < volume->sectors_per_cluster; ++sector_index) {
            memset(sector, 0, sizeof(sector));
            uint16_t copy = 0u;
            if (position < size) {
                const uint32_t remaining = size - position;
                copy = remaining < sizeof(sector) ? (uint16_t)remaining : (uint16_t)sizeof(sector);
                if (copy != 0u && data != NULL) memcpy(sector, &data[position], copy);
                position += copy;
            }
            if (!writer(first_lba + sector_index, sector, context)) return EMES_FAT16_RW_IO;
        }
        ++result->data_cluster_writes;
    }
    return EMES_FAT16_RW_OK;
}

static emes_fat16_rw_result_t publish_new_chain(
    const emes_fat16_volume_t *volume,
    emes_fat16_sector_writer_t writer,
    void *context,
    const uint16_t *clusters,
    uint16_t cluster_count,
    uint8_t fat_copy,
    emes_fat16_commit_result_t *result
) {
    for (uint16_t index = 0u; index < cluster_count; ++index) {
        const uint16_t next = (index + 1u < cluster_count) ? clusters[index + 1u] : 0xFFFFu;
        emes_fat16_rw_result_t status = write_fat_entry(
            volume, writer, context, fat_copy, clusters[index], next
        );
        if (status != EMES_FAT16_RW_OK) return status;
        ++result->fat_entry_writes;
    }
    return EMES_FAT16_RW_OK;
}

static emes_fat16_rw_result_t publish_directory(
    const emes_fat16_volume_t *volume,
    emes_fat16_sector_writer_t writer,
    void *context,
    const emes_fat16_commit_request_t *request,
    uint16_t first_cluster,
    emes_fat16_commit_result_t *result
) {
    uint8_t sector[EMES_FAT16_SECTOR_SIZE];
    if (request->directory_offset > EMES_FAT16_SECTOR_SIZE - 32u ||
        (request->directory_offset & 31u) != 0u) return EMES_FAT16_RW_INVALID;
    if (!volume->read_sector(request->directory_lba, sector, volume->context)) {
        return EMES_FAT16_RW_IO;
    }
    uint8_t *record = &sector[request->directory_offset];
    memset(record, 0, 32u);
    memcpy(record, request->alias_raw, 11u);
    record[11] = (uint8_t)(request->attributes & 0x3Fu);
    put_le16(&record[14], request->create_time);
    put_le16(&record[16], request->create_date);
    /* Deterministic Sprint 9 write timestamp: 2026-07-28 12:00:00. */
    put_le16(&record[18], (uint16_t)(((2026u - 1980u) << 9) | (7u << 5) | 28u));
    put_le16(&record[22], (uint16_t)(12u << 11));
    put_le16(&record[24], (uint16_t)(((2026u - 1980u) << 9) | (7u << 5) | 28u));
    put_le16(&record[26], first_cluster);
    put_le32(&record[28], request->size);
    if (!writer(request->directory_lba, sector, context)) return EMES_FAT16_RW_IO;
    ++result->directory_entry_writes;
    return EMES_FAT16_RW_OK;
}

static emes_fat16_rw_result_t release_old_chain(
    const emes_fat16_volume_t *volume,
    emes_fat16_sector_writer_t writer,
    void *context,
    uint16_t first_cluster,
    emes_fat16_commit_result_t *result
) {
    uint16_t cluster = first_cluster;
    uint32_t guard = 0u;
    while (cluster >= 2u && cluster < 0xFFF8u && guard++ <= volume->cluster_count) {
        uint16_t next = 0u;
        emes_fat16_rw_result_t status = read_fat_entry(volume, cluster, &next);
        if (status != EMES_FAT16_RW_OK) return status;
        for (uint8_t copy = 0u; copy < volume->fat_count; ++copy) {
            status = write_fat_entry(volume, writer, context, copy, cluster, 0u);
            if (status != EMES_FAT16_RW_OK) return status;
            ++result->fat_entry_writes;
        }
        ++result->released_clusters;
        cluster = next;
    }
    return guard > volume->cluster_count + 1u ? EMES_FAT16_RW_INVALID : EMES_FAT16_RW_OK;
}

static emes_fat16_rw_result_t verify_data(
    const emes_fat16_volume_t *volume,
    const uint16_t *clusters,
    uint16_t cluster_count,
    const uint8_t *data,
    uint32_t size
) {
    uint8_t sector[EMES_FAT16_SECTOR_SIZE];
    uint32_t position = 0u;
    for (uint16_t index = 0u; index < cluster_count; ++index) {
        const uint32_t first_lba = volume->data_lba
            + ((uint32_t)clusters[index] - 2u) * volume->sectors_per_cluster;
        for (uint8_t sector_index = 0u; sector_index < volume->sectors_per_cluster && position < size; ++sector_index) {
            if (!volume->read_sector(first_lba + sector_index, sector, volume->context)) {
                return EMES_FAT16_RW_IO;
            }
            const uint32_t remaining = size - position;
            const uint16_t compare = remaining < sizeof(sector) ? (uint16_t)remaining : (uint16_t)sizeof(sector);
            if (compare != 0u && data != NULL && memcmp(sector, &data[position], compare) != 0) {
                return EMES_FAT16_RW_VERIFY_FAILED;
            }
            position += compare;
        }
    }
    return EMES_FAT16_RW_OK;
}

emes_fat16_rw_result_t emes_fat16_rw_commit(
    const emes_fat16_volume_t *volume,
    emes_fat16_sector_writer_t writer,
    void *io_context,
    const emes_fat16_commit_request_t *request,
    emes_fat16_boundary_hook_t boundary_hook,
    void *boundary_context,
    emes_fat16_commit_result_t *result
) {
    if (volume == NULL || writer == NULL || request == NULL || result == NULL ||
        volume->bytes_per_sector != EMES_FAT16_SECTOR_SIZE || volume->fat_count == 0u) {
        return EMES_FAT16_RW_INVALID;
    }
    if (request->write_protect) return EMES_FAT16_RW_WRITE_PROTECT;
    memset(result, 0, sizeof(*result));

    const uint32_t cluster_bytes = (uint32_t)volume->sectors_per_cluster * EMES_FAT16_SECTOR_SIZE;
    const uint32_t needed32 = request->size == 0u ? 0u : (request->size + cluster_bytes - 1u) / cluster_bytes;
    if (needed32 > EMES_FAT16_RW_MAX_CLUSTERS) return EMES_FAT16_RW_NO_SPACE;
    const uint16_t needed = (uint16_t)needed32;
    uint16_t clusters[EMES_FAT16_RW_MAX_CLUSTERS];
    memset(clusters, 0, sizeof(clusters));

    emes_fat16_rw_result_t status = allocate_clusters(volume, needed, clusters);
    if (status != EMES_FAT16_RW_OK) return status;
    result->allocated_clusters = needed;
    result->first_cluster = needed == 0u ? 0u : clusters[0];

    status = write_data_clusters(
        volume, writer, io_context, clusters, needed, request->data, request->size, result
    );
    if (status != EMES_FAT16_RW_OK) return status;
    if (!boundary(EMES_FAT16_COMMIT_AFTER_DATA, boundary_hook, boundary_context, result)) {
        return EMES_FAT16_RW_POWER_LOSS;
    }

    for (uint8_t copy = 0u; copy < volume->fat_count; ++copy) {
        status = publish_new_chain(volume, writer, io_context, clusters, needed, copy, result);
        if (status != EMES_FAT16_RW_OK) return status;
        const emes_fat16_commit_boundary_t phase = copy == 0u
            ? EMES_FAT16_COMMIT_AFTER_FAT_COPY_0
            : EMES_FAT16_COMMIT_AFTER_FAT_COPY_1;
        if (!boundary(phase, boundary_hook, boundary_context, result)) {
            return EMES_FAT16_RW_POWER_LOSS;
        }
    }

    status = publish_directory(volume, writer, io_context, request, result->first_cluster, result);
    if (status != EMES_FAT16_RW_OK) return status;
    if (!boundary(EMES_FAT16_COMMIT_AFTER_DIRECTORY, boundary_hook, boundary_context, result)) {
        return EMES_FAT16_RW_POWER_LOSS;
    }

    status = release_old_chain(volume, writer, io_context, request->old_first_cluster, result);
    if (status != EMES_FAT16_RW_OK) return status;
    if (!boundary(EMES_FAT16_COMMIT_AFTER_CLEANUP, boundary_hook, boundary_context, result)) {
        return EMES_FAT16_RW_POWER_LOSS;
    }

    if (request->verify) {
        status = verify_data(volume, clusters, needed, request->data, request->size);
        if (status != EMES_FAT16_RW_OK) return status;
        ++result->verify_passes;
    }
    if (!boundary(EMES_FAT16_COMMIT_AFTER_VERIFY, boundary_hook, boundary_context, result)) {
        return EMES_FAT16_RW_POWER_LOSS;
    }
    return EMES_FAT16_RW_OK;
}
