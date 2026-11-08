#include "ide_image.h"
#include <string.h>

static void put_le16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void ata_string(uint8_t *out, uint16_t word, uint16_t words, const char *text) {
    size_t length = strlen(text);
    size_t limit = (size_t)words * 2u;
    for (size_t i = 0u; i < limit; i += 2u) {
        uint8_t first = i < length ? (uint8_t)text[i] : (uint8_t)' ';
        uint8_t second = i + 1u < length ? (uint8_t)text[i + 1u] : (uint8_t)' ';
        out[word * 2u + i] = second;
        out[word * 2u + i + 1u] = first;
    }
}

bool emes_ide_attach(emes_ide_image_t *ide, uint8_t unit, uint32_t byte_size, bool read_only) {
    if (ide == NULL || unit > 1u || byte_size < 64u * 512u || (byte_size % 512u) != 0u)
        return false;
    memset(ide, 0, sizeof(*ide));
    ide->unit = unit;
    ide->attached = true;
    ide->read_only = read_only;
    ide->sectors = byte_size / 512u;
    ide->heads = 16u;
    ide->sectors_per_track = 63u;
    uint32_t cylinders = (ide->sectors + 1007u) / 1008u;
    if (cylinders == 0u) cylinders = 1u;
    if (cylinders > 16383u) cylinders = 16383u;
    ide->cylinders = (uint16_t)cylinders;
    return true;
}

bool emes_ide_read_half(const emes_ide_image_t *ide, uint32_t lba, uint8_t half,
                        uint8_t out[256], emes_ide_reader_t reader, void *context) {
    if (ide == NULL || !ide->attached || lba >= ide->sectors || half > 1u ||
        out == NULL || reader == NULL)
        return false;
    return reader(lba * 512u + (uint32_t)half * 256u, out, 256u, context);
}

bool emes_ide_write_half(emes_ide_image_t *ide, uint32_t lba, uint8_t half,
                         const uint8_t data[256], emes_ide_writer_t writer, void *context) {
    if (ide == NULL || !ide->attached || ide->read_only || lba >= ide->sectors ||
        half > 1u || data == NULL || writer == NULL)
        return false;
    if (!writer(lba * 512u + (uint32_t)half * 256u, data, 256u, context))
        return false;
    ide->dirty = true;
    return true;
}

void emes_ide_identify(const emes_ide_image_t *ide, uint8_t out[512]) {
    memset(out, 0, 512u);
    if (ide == NULL || !ide->attached) return;
    put_le16(out + 0, 0x0040u);
    put_le16(out + 2, ide->cylinders);
    put_le16(out + 6, ide->heads);
    put_le16(out + 12, ide->sectors_per_track);
    put_le16(out + 98, 0x0200u);
    put_le16(out + 120, (uint16_t)ide->sectors);
    put_le16(out + 122, (uint16_t)(ide->sectors >> 16));
    put_le16(out + 164, 0x0020u);
    put_le16(out + 170, 0x0020u);
    ata_string(out, 23u, 4u, "0.16.0");
    ata_string(out, 27u, 20u, ide->unit ? "emesDOS09 VIRTUAL IDE1" : "emesDOS09 VIRTUAL IDE0");
}
