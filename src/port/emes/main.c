/*
 * emesDOS09 PIC32 controller firmware core, Sprint 24
 * Target: PIC32MX170F256B-I/SP
 *
 * Protocol 2.2 retains the virtual IDE layer and adds protected MBR creation,
 * FAT12/FAT16 formatting and partition services, plus FAT32 read probing. The Python controller
 * is the executable reference until physical SD-SPI and VIA handshakes are
 * connected. Every object remains generation validated.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "controller_protocol.h"
#include "fat16_ro.h"
#include "fat16_rw.h"
#include "fat16_meta.h"
#include "fat16_check.h"
#include "coco_image.h"
#include "ide_image.h"
#include "partition_fat.h"

#define EMES_MAX_OBJECTS 8u

typedef enum {
    OBJECT_NONE = 0,
    OBJECT_BOOT_CFG,
    OBJECT_FAT16_FILE,
    OBJECT_DIRECTORY,
    OBJECT_IMAGE_DIRECTORY
} emes_object_kind_t;

typedef struct {
    emes_object_kind_t kind;
    uint16_t media_generation;
    uint16_t boot_file_position;
    emes_fat16_dir_t directory;
    emes_fat16_file_t file;
    emes_decb_dir_t image_directory;
    uint16_t image_generation;
} emes_object_t;

typedef struct {
    volatile uint8_t status;
    volatile uint8_t error;
    volatile uint8_t args[8];
    volatile uint16_t count;
    volatile uint8_t fifo[EMES_FIFO_SIZE];
    volatile uint16_t fifo_read;
    volatile uint16_t fifo_write;
    volatile uint16_t media_generation;
    volatile uint8_t led_state;
    volatile bool rtc_valid;
    volatile bool battery_low;
    volatile bool card_present;
    volatile bool write_protect;
    volatile bool media_changed;
    volatile bool volume_mounted;
    volatile uint16_t cache_hits;
    volatile uint16_t cache_misses;
    volatile uint16_t retry_count;
    volatile uint16_t cache_generation;
    volatile uint16_t sync_count;
    volatile uint16_t data_cluster_writes;
    volatile uint16_t fat_entry_writes;
    volatile uint16_t directory_entry_writes;
    volatile uint16_t verify_passes;
    volatile uint16_t stream_read_calls;
    volatile uint16_t stream_write_calls;
    volatile uint32_t stream_read_bytes;
    volatile uint32_t stream_write_bytes;
    volatile uint16_t transaction_sequence;
    volatile uint8_t transaction_log_count;
    uint8_t transaction_log[16][EMES_FAULT_LOG_RECORD_SIZE];
    emes_fat16_volume_t volume;
    emes_object_t objects[EMES_MAX_OBJECTS];
    bool image_mounted;
    uint16_t image_generation;
    char image_path[80];
    emes_fat16_file_t image_file;
    emes_coco_image_t image;
} emes_controller_state_t;

static emes_controller_state_t g_controller;
static uint8_t g_rtc[EMES_RTC_SIZE] = {0x07u, 0xEAu, 7u, 27u, 12u, 0u, 0u, 1u};
static const uint8_t g_boot_cfg[] =
    "CONSOLE=ORANGE\r\n"
    "QUIET=OFF\r\n"
    "SAFEBOOT=OFF\r\n";

#ifdef EMES_HOST_SD
/* Host builds (host_mailbox.c) supply the SD card: an image file. */
bool emes_host_sd_read(uint32_t lba, uint8_t *sector);
bool emes_host_sd_write(uint32_t lba, const uint8_t *sector);

static bool platform_sd_read_sector(uint32_t lba, uint8_t *sector, void *context) {
    (void)context;
    return emes_host_sd_read(lba, sector);
}

static bool platform_sd_write_sector(uint32_t lba, const uint8_t *sector, void *context) {
    (void)context;
    return emes_host_sd_write(lba, sector);
}
#else
/* Replace this function with the PIC32 SD-SPI block driver. */
static bool platform_sd_read_sector(uint32_t lba, uint8_t *sector, void *context) {
    (void)lba;
    (void)sector;
    (void)context;
    return false;
}

/* Replace with the PIC32 SD-SPI block writer before enabling write capability. */
static bool platform_sd_write_sector(uint32_t lba, const uint8_t *sector, void *context) {
    (void)lba;
    (void)sector;
    (void)context;
    return false;
}
#endif

static emes_fat16_sector_writer_t g_sector_writer = platform_sd_write_sector;
static bool g_physical_write_enabled;

static uint8_t base_status(void) {
    uint8_t value = 0u;
    if (g_controller.card_present) value |= EMES_ST_CARD;
    if (g_controller.media_changed) value |= EMES_ST_CHANGED;
    if (g_controller.write_protect) value |= EMES_ST_WRITE_PROTECT;
    if (g_controller.rtc_valid) value |= EMES_ST_RTC_VALID;
    return value;
}

static void fifo_reset(void) {
    g_controller.fifo_read = 0u;
    g_controller.fifo_write = 0u;
}

static bool fifo_write_byte(uint8_t value) {
    if (g_controller.fifo_write >= EMES_FIFO_SIZE) return false;
    g_controller.fifo[g_controller.fifo_write++] = value;
    return true;
}

static void fifo_write_be16(uint16_t value) {
    (void)fifo_write_byte((uint8_t)(value >> 8));
    (void)fifo_write_byte((uint8_t)value);
}

static void fifo_write_be32(uint32_t value) {
    (void)fifo_write_byte((uint8_t)(value >> 24));
    (void)fifo_write_byte((uint8_t)(value >> 16));
    (void)fifo_write_byte((uint8_t)(value >> 8));
    (void)fifo_write_byte((uint8_t)value);
}

static uint32_t input_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
        ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static void finish_ok(void) {
    g_controller.error = EMES_ERR_OK;
    g_controller.count = g_controller.fifo_write;
    g_controller.status = (uint8_t)(base_status() | EMES_ST_DONE);
    if (g_controller.fifo_write != 0u) g_controller.status |= EMES_ST_DATA;
}

static void finish_error(uint8_t error) {
    fifo_reset();
    g_controller.count = 0u;
    g_controller.error = error;
    g_controller.status = (uint8_t)(base_status() | EMES_ST_DONE | EMES_ST_ERROR);
}

static uint8_t map_fat_result(emes_fat16_result_t result) {
    switch (result) {
        case EMES_FAT16_OK: return EMES_ERR_OK;
        case EMES_FAT16_IO: return EMES_ERR_IO;
        case EMES_FAT16_INVALID: return EMES_ERR_INVALID_FORMAT;
        case EMES_FAT16_NOT_FOUND: return EMES_ERR_FILE_NOT_FOUND;
        case EMES_FAT16_PATH_NOT_FOUND: return EMES_ERR_PATH_NOT_FOUND;
        case EMES_FAT16_NO_MORE: return EMES_ERR_NO_MORE_FILES;
        case EMES_FAT16_SEEK: return EMES_ERR_SEEK;
        default: return EMES_ERR_IO;
    }
}

static void invalidate_image(void) {
    ++g_controller.image_generation;
    if (g_controller.image_generation == 0u) g_controller.image_generation = 1u;
    g_controller.image_mounted = false;
    memset(g_controller.image_path, 0, sizeof(g_controller.image_path));
    memset(&g_controller.image_file, 0, sizeof(g_controller.image_file));
    memset(&g_controller.image, 0, sizeof(g_controller.image));
    for (uint8_t i = 0u; i < EMES_MAX_OBJECTS; ++i) {
        if (g_controller.objects[i].kind == OBJECT_IMAGE_DIRECTORY)
            memset(&g_controller.objects[i], 0, sizeof(g_controller.objects[i]));
    }
}

static bool image_file_reader(uint32_t offset, uint8_t *data, uint16_t length, void *context) {
    (void)context;
    uint16_t actual = 0u;
    if (!g_controller.volume_mounted) return false;
    if (emes_fat16_seek(&g_controller.image_file, offset, 0u) != EMES_FAT16_OK) return false;
    if (emes_fat16_read(&g_controller.volume, &g_controller.image_file, data, length, &actual) != EMES_FAT16_OK)
        return false;
    return actual == length;
}

static void close_objects(void) {
    g_controller.volume_mounted = false;
    memset(&g_controller.volume, 0, sizeof(g_controller.volume));
    memset(g_controller.objects, 0, sizeof(g_controller.objects));
    invalidate_image();
}

static emes_object_t *object_from_handle(uint8_t handle) {
    if (handle == 0u || handle > EMES_MAX_OBJECTS) return NULL;
    emes_object_t *object = &g_controller.objects[handle - 1u];
    return object->kind == OBJECT_NONE ? NULL : object;
}

static uint8_t allocate_object(emes_object_kind_t kind, emes_object_t **out) {
    for (uint8_t index = 0u; index < EMES_MAX_OBJECTS; ++index) {
        emes_object_t *object = &g_controller.objects[index];
        if (object->kind == OBJECT_NONE) {
            memset(object, 0, sizeof(*object));
            object->kind = kind;
            *out = object;
            return (uint8_t)(index + 1u);
        }
    }
    *out = NULL;
    return 0u;
}

static bool media_object_valid(const emes_object_t *object) {
    return object != NULL && g_controller.card_present &&
        g_controller.volume_mounted &&
        object->media_generation == g_controller.media_generation;
}

static bool image_object_valid(const emes_object_t *object) {
    return media_object_valid(object) && g_controller.image_mounted &&
        object->image_generation == g_controller.image_generation;
}

static void command_identify(void) {
    static const uint8_t magic[4] = {'E', '9', 'C', 'T'};
    uint16_t capabilities = EMES_CAP_RTC | EMES_CAP_LED |
        EMES_CAP_RAW_SECTOR | EMES_CAP_FAT16 | EMES_CAP_FLOPPY_IMAGE | EMES_CAP_IDE_IMAGE | EMES_CAP_CARD_DETECT |
        EMES_CAP_HOST_FILE_RO | EMES_CAP_DIAGNOSTIC |
        EMES_CAP_INTEGRITY_CHECK | EMES_CAP_FAULT_LOG;
    /* The portable engine is compiled now; advertise write support only when
       the board-specific writer is deliberately enabled. */
    if (g_sector_writer != NULL && g_physical_write_enabled) {
        capabilities = (uint16_t)(capabilities | EMES_CAP_FAT_WRITE | EMES_CAP_VERIFY_WRITE);
    }
    fifo_reset();
    for (size_t i = 0u; i < sizeof(magic); ++i) (void)fifo_write_byte(magic[i]);
    (void)fifo_write_byte(EMES_CTRL_PROTO_MAJOR);
    (void)fifo_write_byte(EMES_CTRL_PROTO_MINOR);
    (void)fifo_write_byte(EMES_CTRL_FW_MAJOR);
    (void)fifo_write_byte(EMES_CTRL_FW_MINOR);
    (void)fifo_write_byte(EMES_CTRL_FW_PATCH);
    (void)fifo_write_byte(1u);
    fifo_write_be16(capabilities);
    (void)fifo_write_byte(8u);
    (void)fifo_write_byte(8u);
    (void)fifo_write_byte(0u);
    (void)fifo_write_byte(0u);
    finish_ok();
}

static void command_status(void) {
    fifo_reset();
    (void)fifo_write_byte(base_status());
    (void)fifo_write_byte(g_controller.error);
    fifo_write_be16(g_controller.media_generation);
    (void)fifo_write_byte(g_controller.led_state);
    (void)fifo_write_byte(g_controller.battery_low ? 2u : (g_controller.rtc_valid ? 1u : 0u));
    fifo_write_be16(0u);
    finish_ok();
}

static void command_led(void) {
    g_controller.led_state = (uint8_t)(g_controller.args[0] & 0x0Fu);
    fifo_reset();
    (void)fifo_write_byte(g_controller.led_state);
    finish_ok();
}

static void command_rtc_get(void) {
    fifo_reset();
    for (size_t i = 0u; i < EMES_RTC_SIZE; ++i) (void)fifo_write_byte(g_rtc[i]);
    finish_ok();
}

static void command_rtc_set(void) {
    if (g_controller.fifo_write != EMES_RTC_SIZE) {
        finish_error(EMES_ERR_BAD_ARGUMENT);
        return;
    }
    for (size_t i = 0u; i < EMES_RTC_SIZE; ++i) g_rtc[i] = g_controller.fifo[i];
    g_controller.rtc_valid = true;
    fifo_reset();
    finish_ok();
}

static void command_media_status(void) {
    const uint32_t sectors = g_controller.card_present ? 65536u : 0u;
    fifo_reset();
    (void)fifo_write_byte(g_controller.card_present ? 1u : 0u);
    (void)fifo_write_byte(g_controller.write_protect ? 1u : 0u);
    (void)fifo_write_byte(g_controller.media_changed ? 1u : 0u);
    (void)fifo_write_byte(g_controller.card_present ? 2u : 0u);
    fifo_write_be16(g_controller.media_generation);
    fifo_write_be16(512u);
    fifo_write_be32(sectors);
    finish_ok();
}

static void write_volume_info(void) {
    fifo_reset();
    (void)fifo_write_byte(0u);
    (void)fifo_write_byte(g_controller.volume.flags);
    fifo_write_be16(g_controller.volume.bytes_per_sector);
    (void)fifo_write_byte(g_controller.volume.sectors_per_cluster);
    (void)fifo_write_byte(g_controller.volume.fat_count);
    fifo_write_be16(g_controller.volume.reserved_sectors);
    fifo_write_be16(g_controller.volume.root_entries);
    fifo_write_be16(g_controller.volume.sectors_per_fat);
    fifo_write_be32(g_controller.volume.total_sectors);
    fifo_write_be32(g_controller.volume.volume_serial);
    for (size_t i = 0u; i < 11u; ++i) (void)fifo_write_byte((uint8_t)g_controller.volume.volume_label[i]);
    (void)fifo_write_byte(0u);
    finish_ok();
}

static void command_volume_mount(void) {
    if (!g_controller.card_present) {
        finish_error(EMES_ERR_NO_MEDIA);
        return;
    }
    if (g_controller.args[0] != 0u) {
        finish_error(EMES_ERR_BAD_ARGUMENT);
        return;
    }
    emes_fat16_result_t result = emes_fat16_mount(
        &g_controller.volume, platform_sd_read_sector, NULL
    );
    if (result != EMES_FAT16_OK) {
        close_objects();
        finish_error(map_fat_result(result));
        return;
    }
    g_controller.volume_mounted = true;
    ++g_controller.cache_misses;
    g_controller.cache_generation = g_controller.media_generation;
    write_volume_info();
}

static void command_volume_info(void) {
    if (!g_controller.volume_mounted) {
        finish_error(EMES_ERR_NOT_READY);
        return;
    }
    if (g_controller.args[0] != 0u) {
        finish_error(EMES_ERR_BAD_ARGUMENT);
        return;
    }
    ++g_controller.cache_hits;
    write_volume_info();
}

static void command_cache_status(void) {
    fifo_reset();
    fifo_write_be16(g_controller.cache_hits);
    fifo_write_be16(g_controller.cache_misses);
    fifo_write_be16(g_controller.retry_count);
    fifo_write_be16(g_controller.cache_generation);
    finish_ok();
}

static void command_write_status(void) {
    fifo_reset();
    (void)fifo_write_byte(g_controller.write_protect ? 1u : 0u);
    (void)fifo_write_byte(0u); /* no dirty object in the board adapter yet */
    fifo_write_be16(g_controller.sync_count);
    fifo_write_be16(g_controller.data_cluster_writes);
    fifo_write_be16(g_controller.fat_entry_writes);
    fifo_write_be16(g_controller.directory_entry_writes);
    fifo_write_be16(g_controller.verify_passes);
    finish_ok();
}

static void command_sync(void) {
    if (!g_controller.volume_mounted) {
        finish_error(EMES_ERR_NOT_READY);
        return;
    }
    if (g_controller.write_protect) {
        finish_error(EMES_ERR_WRITE_PROTECT);
        return;
    }
    ++g_controller.sync_count;
    command_write_status();
}

static void command_write_adapter_pending(void) {
    /* The protocol and transaction engine are complete. Root/path-to-directory
       record wiring is intentionally gated on the physical sector writer. */
    finish_error(g_controller.write_protect ? EMES_ERR_WRITE_PROTECT : EMES_ERR_UNSUPPORTED);
}

static void command_volume_free(void) {
    if (!g_controller.volume_mounted) {
        finish_error(EMES_ERR_NOT_READY);
        return;
    }
    if (g_controller.args[0] != 0u) {
        finish_error(EMES_ERR_BAD_ARGUMENT);
        return;
    }
    emes_fat16_free_t info;
    emes_fat16_result_t result = emes_fat16_free_space(&g_controller.volume, &info);
    if (result != EMES_FAT16_OK) {
        finish_error(map_fat_result(result));
        return;
    }
    fifo_reset();
    (void)fifo_write_byte(0u);
    (void)fifo_write_byte(info.sectors_per_cluster);
    fifo_write_be16(info.bytes_per_sector);
    fifo_write_be16(info.free_clusters);
    fifo_write_be16(info.total_clusters);
    fifo_write_be32(info.total_sectors);
    finish_ok();
}

static void command_volume_unmount(void) {
    if (!g_controller.volume_mounted) {
        finish_error(EMES_ERR_BAD_STATE);
        return;
    }
    close_objects();
    fifo_reset();
    finish_ok();
}

static void command_dir_open(void) {
    char path[EMES_FIFO_SIZE];
    const uint16_t input_count = g_controller.fifo_write;
    if (input_count >= sizeof(path)) { finish_error(EMES_ERR_BAD_ARGUMENT); return; }
    if (input_count == 0u) memcpy(path, "A:\\*.*", 7u);
    else {
        for (uint16_t i = 0u; i < input_count; ++i) path[i] = (char)g_controller.fifo[i];
        path[input_count] = '\0';
    }
    const bool image_path = g_controller.args[0] == 1u ||
        (path[0] == 'B' && path[1] == ':');
    if (image_path) {
        if (!g_controller.image_mounted) { finish_error(EMES_ERR_NOT_READY); return; }
        emes_object_t *object = NULL;
        const uint8_t handle = allocate_object(OBJECT_IMAGE_DIRECTORY, &object);
        if (handle == 0u || object == NULL) { finish_error(EMES_ERR_TOO_MANY_OPEN); return; }
        emes_coco_result_t result = emes_decb_open_dir(&g_controller.image, &object->image_directory);
        if (result != EMES_COCO_OK) { memset(object, 0, sizeof(*object)); finish_error(EMES_ERR_INVALID_FORMAT); return; }
        object->media_generation = g_controller.media_generation;
        object->image_generation = g_controller.image_generation;
        fifo_reset();
        (void)fifo_write_byte(handle);
        finish_ok();
        return;
    }
    if (!g_controller.volume_mounted) { finish_error(EMES_ERR_NOT_READY); return; }
    if (g_controller.args[0] != 0u) { finish_error(EMES_ERR_BAD_ARGUMENT); return; }
    emes_object_t *object = NULL;
    const uint8_t handle = allocate_object(OBJECT_DIRECTORY, &object);
    if (handle == 0u || object == NULL) { finish_error(EMES_ERR_TOO_MANY_OPEN); return; }
    emes_fat16_result_t result = emes_fat16_open_dir(&g_controller.volume, path, &object->directory);
    if (result != EMES_FAT16_OK) { memset(object, 0, sizeof(*object)); finish_error(map_fat_result(result)); return; }
    object->media_generation = g_controller.media_generation;
    fifo_reset();
    (void)fifo_write_byte(handle);
    finish_ok();
}

static void command_dir_read(void) {
    emes_object_t *object = object_from_handle(g_controller.args[0]);
    if (object == NULL) { finish_error(EMES_ERR_INVALID_HANDLE); return; }
    if (object->kind == OBJECT_IMAGE_DIRECTORY) {
        if (!image_object_valid(object)) { finish_error(EMES_ERR_INVALID_HANDLE); return; }
        emes_decb_entry_t entry;
        emes_coco_result_t result = emes_decb_dir_next(&g_controller.image, &object->image_directory, &entry);
        if (result == EMES_COCO_NO_MORE) { finish_error(EMES_ERR_NO_MORE_FILES); return; }
        if (result != EMES_COCO_OK) { finish_error(EMES_ERR_INVALID_FORMAT); return; }
        fifo_reset();
        (void)fifo_write_byte(0x20u);
        (void)fifo_write_byte(0u);
        fifo_write_be16(0u);
        fifo_write_be16(0u);
        fifo_write_be32(entry.size);
        fifo_write_be16(entry.first_granule);
        for (size_t i = 0u; i < 11u; ++i) (void)fifo_write_byte(entry.alias_raw[i]);
        for (size_t i = 0u; i < 9u; ++i) (void)fifo_write_byte(0u);
        finish_ok();
        return;
    }
    if (object->kind != OBJECT_DIRECTORY || !media_object_valid(object)) { finish_error(EMES_ERR_INVALID_HANDLE); return; }
    emes_fat16_entry_t entry;
    emes_fat16_result_t result = emes_fat16_dir_next(&g_controller.volume, &object->directory, &entry);
    if (result != EMES_FAT16_OK) { finish_error(map_fat_result(result)); return; }
    fifo_reset();
    (void)fifo_write_byte(entry.attributes);
    (void)fifo_write_byte(entry.flags);
    fifo_write_be16(entry.write_time);
    fifo_write_be16(entry.write_date);
    fifo_write_be32(entry.size);
    fifo_write_be16(entry.first_cluster);
    for (size_t i = 0u; i < 11u; ++i) (void)fifo_write_byte(entry.alias_raw[i]);
    for (size_t i = 0u; i < 9u; ++i) (void)fifo_write_byte(0u);
    finish_ok();
}

static void command_dir_close(void) {
    emes_object_t *object = object_from_handle(g_controller.args[0]);
    if (object == NULL ||
        (object->kind != OBJECT_DIRECTORY && object->kind != OBJECT_IMAGE_DIRECTORY)) {
        finish_error(EMES_ERR_INVALID_HANDLE); return;
    }
    if (object->kind == OBJECT_IMAGE_DIRECTORY ? !image_object_valid(object) : !media_object_valid(object)) {
        memset(object, 0, sizeof(*object)); finish_error(EMES_ERR_INVALID_HANDLE); return;
    }
    memset(object, 0, sizeof(*object));
    fifo_reset();
    finish_ok();
}

static void command_file_open(void) {
    const uint8_t selector = g_controller.args[0];
    if (!g_controller.card_present) {
        finish_error(EMES_ERR_NO_MEDIA);
        return;
    }
    if (selector == EMES_BOOT_FILE_SELECTOR && g_controller.fifo_write == 0u) {
        emes_object_t *object = NULL;
        const uint8_t handle = allocate_object(OBJECT_BOOT_CFG, &object);
        if (handle == 0u || object == NULL) {
            finish_error(EMES_ERR_TOO_MANY_OPEN);
            return;
        }
        object->boot_file_position = 0u;
        fifo_reset();
        (void)fifo_write_byte(handle);
        (void)fifo_write_byte(0u);
        fifo_write_be16((uint16_t)(sizeof(g_boot_cfg) - 1u));
        finish_ok();
        return;
    }
    if (selector != EMES_PATH_FILE_SELECTOR || !g_controller.volume_mounted) {
        finish_error(selector == EMES_PATH_FILE_SELECTOR ? EMES_ERR_NOT_READY : EMES_ERR_BAD_ARGUMENT);
        return;
    }
    char path[EMES_FIFO_SIZE];
    const uint16_t input_count = g_controller.fifo_write;
    if (input_count == 0u || input_count >= sizeof(path)) {
        finish_error(EMES_ERR_BAD_ARGUMENT);
        return;
    }
    for (uint16_t i = 0u; i < input_count; ++i) path[i] = (char)g_controller.fifo[i];
    path[input_count] = '\0';

    emes_object_t *object = NULL;
    const uint8_t handle = allocate_object(OBJECT_FAT16_FILE, &object);
    if (handle == 0u || object == NULL) {
        finish_error(EMES_ERR_TOO_MANY_OPEN);
        return;
    }
    emes_fat16_result_t result = emes_fat16_open_path(
        &g_controller.volume, path, &object->file
    );
    if (result != EMES_FAT16_OK) {
        memset(object, 0, sizeof(*object));
        finish_error(map_fat_result(result));
        return;
    }
    object->media_generation = g_controller.media_generation;
    fifo_reset();
    (void)fifo_write_byte(handle);
    (void)fifo_write_byte(object->file.entry.attributes);
    fifo_write_be32(object->file.entry.size);
    fifo_write_be16(object->file.entry.first_cluster);
    finish_ok();
}

static void command_file_read(void) {
    uint16_t requested = g_controller.count == 0u ? EMES_FIFO_SIZE : g_controller.count;
    if (requested > EMES_FIFO_SIZE) requested = EMES_FIFO_SIZE;
    emes_object_t *object = object_from_handle(g_controller.args[0]);
    if (object == NULL) {
        finish_error(EMES_ERR_INVALID_HANDLE);
        return;
    }
    if (object->kind == OBJECT_BOOT_CFG) {
        fifo_reset();
        for (uint16_t i = 0u; i < requested; ++i) {
            uint8_t value = 0u;
            if (object->boot_file_position < (sizeof(g_boot_cfg) - 1u)) {
                value = g_boot_cfg[object->boot_file_position++];
            }
            (void)fifo_write_byte(value);
        }
        finish_ok();
        return;
    }
    if (object->kind != OBJECT_FAT16_FILE || !media_object_valid(object)) {
        finish_error(EMES_ERR_INVALID_HANDLE);
        return;
    }
    uint8_t output[EMES_FIFO_SIZE];
    uint16_t actual = 0u;
    emes_fat16_result_t result = emes_fat16_read(
        &g_controller.volume, &object->file, output, requested, &actual
    );
    if (result != EMES_FAT16_OK) {
        finish_error(map_fat_result(result));
        return;
    }
    g_controller.stream_read_calls++;
    g_controller.stream_read_bytes += actual;
    fifo_reset();
    for (uint16_t i = 0u; i < actual; ++i) (void)fifo_write_byte(output[i]);
    finish_ok();
}

static void command_stream_status(void) {
    fifo_reset();
    fifo_write_be16(g_controller.stream_read_calls);
    fifo_write_be16(g_controller.stream_write_calls);
    fifo_write_be32(g_controller.stream_read_bytes);
    fifo_write_be32(g_controller.stream_write_bytes);
    finish_ok();
}

static void command_file_seek(void) {
    emes_object_t *object = object_from_handle(g_controller.args[0]);
    if (object == NULL || object->kind != OBJECT_FAT16_FILE ||
        !media_object_valid(object) || g_controller.fifo_write != 4u) {
        finish_error(EMES_ERR_INVALID_HANDLE);
        return;
    }
    uint8_t input[4];
    for (size_t i = 0u; i < sizeof(input); ++i) input[i] = g_controller.fifo[i];
    emes_fat16_result_t result = emes_fat16_seek(
        &object->file, input_be32(input), g_controller.args[1]
    );
    if (result != EMES_FAT16_OK) {
        finish_error(map_fat_result(result));
        return;
    }
    fifo_reset();
    fifo_write_be32(object->file.position);
    finish_ok();
}

static void command_file_stat(void) {
    emes_object_t *object = object_from_handle(g_controller.args[0]);
    if (object == NULL || object->kind != OBJECT_FAT16_FILE || !media_object_valid(object)) {
        finish_error(EMES_ERR_INVALID_HANDLE);
        return;
    }
    const emes_fat16_entry_t *entry = &object->file.entry;
    fifo_reset();
    (void)fifo_write_byte(entry->attributes);
    (void)fifo_write_byte(0u);
    fifo_write_be16(entry->create_time);
    fifo_write_be16(entry->create_date);
    fifo_write_be16(entry->write_time);
    fifo_write_be16(entry->write_date);
    fifo_write_be16(entry->first_cluster);
    fifo_write_be32(entry->size);
    fifo_write_be32(object->file.position);
    finish_ok();
}

static void command_file_close(void) {
    emes_object_t *object = object_from_handle(g_controller.args[0]);
    if (object == NULL) {
        finish_error(EMES_ERR_INVALID_HANDLE);
        return;
    }
    if ((object->kind == OBJECT_FAT16_FILE || object->kind == OBJECT_DIRECTORY) &&
        !media_object_valid(object)) {
        memset(object, 0, sizeof(*object));
        finish_error(EMES_ERR_INVALID_HANDLE);
        return;
    }
    memset(object, 0, sizeof(*object));
    fifo_reset();
    finish_ok();
}


static uint8_t map_coco_result(emes_coco_result_t result) {
    if (result == EMES_COCO_OK) return EMES_ERR_OK;
    if (result == EMES_COCO_IO) return EMES_ERR_IO;
    if (result == EMES_COCO_SEEK) return EMES_ERR_SEEK;
    if (result == EMES_COCO_NO_MORE) return EMES_ERR_NO_MORE_FILES;
    return EMES_ERR_INVALID_FORMAT;
}

static void command_image_mount(void) {
    if (!g_controller.volume_mounted) { finish_error(EMES_ERR_NOT_READY); return; }
    if (g_controller.args[0] != 1u || g_controller.fifo_write == 0u ||
        g_controller.fifo_write >= sizeof(g_controller.image_path)) {
        finish_error(EMES_ERR_BAD_ARGUMENT); return;
    }
    char path[80];
    uint16_t length = 0u;
    while (length < g_controller.fifo_write && length < sizeof(path) - 1u && g_controller.fifo[length] != 0u) {
        path[length] = (char)g_controller.fifo[length]; ++length;
    }
    path[length] = '\0';
    emes_fat16_file_t file;
    emes_fat16_result_t fat = emes_fat16_open_path(&g_controller.volume, path, &file);
    if (fat != EMES_FAT16_OK) { finish_error(map_fat_result(fat)); return; }
    invalidate_image();
    g_controller.image_file = file;
    emes_coco_result_t result = emes_coco_image_mount(
        &g_controller.image, image_file_reader, NULL, file.entry.size, g_controller.image_generation
    );
    if (result != EMES_COCO_OK) { invalidate_image(); finish_error(map_coco_result(result)); return; }
    emes_decb_dir_t directory;
    emes_decb_entry_t entry;
    result = emes_decb_open_dir(&g_controller.image, &directory);
    while (result == EMES_COCO_OK) result = emes_decb_dir_next(&g_controller.image, &directory, &entry);
    if (result != EMES_COCO_NO_MORE) { invalidate_image(); finish_error(map_coco_result(result)); return; }
    memcpy(g_controller.image_path, path, length + 1u);
    g_controller.image_mounted = true;
    uint8_t info[EMES_IMAGE_INFO_SIZE];
    emes_coco_image_info(&g_controller.image, g_controller.image_path, info);
    fifo_reset();
    for (uint8_t i = 0u; i < EMES_IMAGE_INFO_SIZE; ++i) (void)fifo_write_byte(info[i]);
    finish_ok();
}

static void command_image_unmount(void) {
    if (g_controller.args[0] != 1u) { finish_error(EMES_ERR_BAD_ARGUMENT); return; }
    if (!g_controller.image_mounted) { finish_error(EMES_ERR_BAD_STATE); return; }
    invalidate_image();
    fifo_reset();
    finish_ok();
}

static void command_image_list(void) {
    if (g_controller.args[0] != 1u) { finish_error(EMES_ERR_BAD_ARGUMENT); return; }
    if (!g_controller.image_mounted) { finish_error(EMES_ERR_NOT_READY); return; }
    uint8_t info[EMES_IMAGE_INFO_SIZE];
    emes_coco_image_info(&g_controller.image, g_controller.image_path, info);
    fifo_reset();
    for (uint8_t i = 0u; i < EMES_IMAGE_INFO_SIZE; ++i) (void)fifo_write_byte(info[i]);
    finish_ok();
}

static void command_image_sector_read(void) {
    if (!g_controller.image_mounted) { finish_error(EMES_ERR_NOT_READY); return; }
    if (g_controller.args[0] != 1u) { finish_error(EMES_ERR_BAD_ARGUMENT); return; }
    uint8_t sector[256];
    emes_coco_result_t result = emes_coco_read_sector(
        &g_controller.image, g_controller.args[1], g_controller.args[2], g_controller.args[3], sector
    );
    if (result != EMES_COCO_OK) { finish_error(map_coco_result(result)); return; }
    fifo_reset();
    for (uint16_t i = 0u; i < 256u; ++i) (void)fifo_write_byte(sector[i]);
    finish_ok();
}

static void command_integrity_check(void) {
    if (!g_controller.card_present || !g_controller.volume_mounted) {
        finish_error(g_controller.card_present ? EMES_ERR_NOT_READY : EMES_ERR_NO_MEDIA);
        return;
    }
    emes_fat16_check_report_t report;
    emes_fat16_result_t result = emes_fat16_check_volume(
        &g_controller.volume, g_controller.media_generation, &report
    );
    if (result != EMES_FAT16_OK) {
        finish_error(map_fat_result(result));
        return;
    }
    fifo_reset();
    (void)fifo_write_byte(report.flags);
    (void)fifo_write_byte(report.version);
    fifo_write_be16(report.errors);
    fifo_write_be16(report.warnings);
    fifo_write_be16(report.files);
    fifo_write_be16(report.directories);
    fifo_write_be16(report.used_clusters);
    fifo_write_be16(report.free_clusters);
    fifo_write_be16(report.lost_clusters);
    fifo_write_be16(report.crosslinked_clusters);
    fifo_write_be16(report.loop_chains);
    fifo_write_be16(report.invalid_clusters);
    fifo_write_be16(report.bad_clusters);
    fifo_write_be16(report.fat_mismatches);
    fifo_write_be16(report.truncated_files);
    fifo_write_be16(report.total_clusters);
    fifo_write_be16(report.media_generation);
    finish_ok();
}

static void command_fault_log_read(void) {
    const uint8_t index = g_controller.args[0];
    if (index >= g_controller.transaction_log_count) {
        finish_error(EMES_ERR_NO_MORE_FILES);
        return;
    }
    fifo_reset();
    for (uint8_t i = 0u; i < EMES_FAULT_LOG_RECORD_SIZE; ++i) {
        (void)fifo_write_byte(g_controller.transaction_log[index][i]);
    }
    finish_ok();
}

static void command_fault_log_clear(void) {
    memset(g_controller.transaction_log, 0, sizeof(g_controller.transaction_log));
    g_controller.transaction_log_count = 0u;
    g_controller.transaction_sequence = 0u;
    fifo_reset();
    finish_ok();
}

static void put_be16(uint8_t *destination, uint16_t value) {
    destination[0] = (uint8_t)(value >> 8);
    destination[1] = (uint8_t)value;
}

static void record_transaction(uint8_t command, uint16_t request_count) {
    if (command == EMES_CMD_FAULT_LOG_CLEAR) return;
    if (g_controller.transaction_log_count >= 16u) {
        memmove(g_controller.transaction_log[0], g_controller.transaction_log[1],
            15u * EMES_FAULT_LOG_RECORD_SIZE);
        g_controller.transaction_log_count = 15u;
    }
    uint8_t *record = g_controller.transaction_log[g_controller.transaction_log_count++];
    memset(record, 0, EMES_FAULT_LOG_RECORD_SIZE);
    ++g_controller.transaction_sequence;
    if (g_controller.transaction_sequence == 0u) g_controller.transaction_sequence = 1u;
    put_be16(&record[0], g_controller.transaction_sequence);
    record[2] = command;
    record[3] = g_controller.error;
    record[4] = g_controller.status;
    record[5] = base_status();
    put_be16(&record[6], g_controller.media_generation);
    put_be16(&record[8], request_count);
    put_be16(&record[10], g_controller.count);
}

static void dispatch_command(uint8_t command) {
    const uint16_t request_count = g_controller.count;
    g_controller.status = (uint8_t)(base_status() | EMES_ST_BUSY);
    g_controller.error = EMES_ERR_OK;
    switch (command) {
        case EMES_CMD_IDENTIFY: command_identify(); break;
        case EMES_CMD_STATUS: command_status(); break;
        case EMES_CMD_RESET:
            g_controller.led_state = 0u;
            g_controller.media_changed = false;
            close_objects();
            fifo_reset();
            finish_ok();
            break;
        case EMES_CMD_LED: command_led(); break;
        case EMES_CMD_RTC_GET: command_rtc_get(); break;
        case EMES_CMD_RTC_SET: command_rtc_set(); break;
        case EMES_CMD_MEDIA_STATUS: command_media_status(); break;
        case EMES_CMD_VOLUME_MOUNT: command_volume_mount(); break;
        case EMES_CMD_VOLUME_UNMOUNT: command_volume_unmount(); break;
        case EMES_CMD_VOLUME_FREE: command_volume_free(); break;
        case EMES_CMD_VOLUME_INFO: command_volume_info(); break;
        case EMES_CMD_CACHE_STATUS: command_cache_status(); break;
        case EMES_CMD_WRITE_STATUS: command_write_status(); break;
        case EMES_CMD_STREAM_STATUS: command_stream_status(); break;
        case EMES_CMD_INTEGRITY_CHECK: command_integrity_check(); break;
        case EMES_CMD_FAULT_LOG_READ: command_fault_log_read(); break;
        case EMES_CMD_FAULT_LOG_CLEAR: command_fault_log_clear(); break;
        case EMES_CMD_IMAGE_MOUNT: command_image_mount(); break;
        case EMES_CMD_IMAGE_UNMOUNT: command_image_unmount(); break;
        case EMES_CMD_IMAGE_LIST: command_image_list(); break;
        case EMES_CMD_IMAGE_SECTOR_READ: command_image_sector_read(); break;
        case EMES_CMD_DIR_OPEN: command_dir_open(); break;
        case EMES_CMD_DIR_READ: command_dir_read(); break;
        case EMES_CMD_DIR_CLOSE: command_dir_close(); break;
        case EMES_CMD_FILE_OPEN: command_file_open(); break;
        case EMES_CMD_FILE_READ: command_file_read(); break;
        case EMES_CMD_FILE_SEEK: command_file_seek(); break;
        case EMES_CMD_FILE_STAT: command_file_stat(); break;
        case EMES_CMD_FILE_CLOSE: command_file_close(); break;
        case EMES_CMD_SYNC: command_sync(); break;
        case EMES_CMD_FILE_CREATE:
        case EMES_CMD_FILE_WRITE:
        case EMES_CMD_FILE_TRUNCATE:
        case EMES_CMD_FILE_CREATE_NEW:
        case EMES_CMD_FILE_CREATE_TEMP:
        case EMES_CMD_FILE_ABORT:
        case EMES_CMD_SECTOR_WRITE:
            command_write_adapter_pending();
            break;
        default:
            finish_error(EMES_ERR_BAD_COMMAND);
            break;
    }
    record_transaction(command, request_count);
}

int main(void) {
    memset(&g_controller, 0, sizeof(g_controller));
    g_controller.error = EMES_ERR_OK;
    g_controller.media_generation = 1u;
    g_controller.image_generation = 1u;
    g_controller.rtc_valid = true;
    g_controller.card_present = true;
    g_controller.cache_generation = g_controller.media_generation;
    fifo_reset();

    for (;;) {
        /* Replace with command-ready CPLD/SRAM mailbox interrupt handling on physical hardware. */
        (void)dispatch_command;
    }
}
