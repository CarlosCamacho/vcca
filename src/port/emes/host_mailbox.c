/*
 * Host mailbox shim: see include/host_mailbox.h. Compiled together with the
 * unmodified firmware core by including main.c, so it can reach the core's
 * static state. Build with -DEMES_HOST_SD.
 */
#define main emes_firmware_main
#include "main.c"
#undef main

#include "host_mailbox.h"

static uint8_t g_mb[16];
static uint8_t g_fifo_ptr;
static uint8_t g_rom_page;
static uint32_t g_commands;

void emes_host_init(void) {
    /* Same initial state as main() in main.c. */
    memset(&g_controller, 0, sizeof(g_controller));
    g_controller.error = EMES_ERR_OK;
    g_controller.media_generation = 1u;
    g_controller.image_generation = 1u;
    g_controller.rtc_valid = true;
    g_controller.card_present = true;
    g_controller.cache_generation = g_controller.media_generation;
    fifo_reset();
    memset(g_mb, 0, sizeof(g_mb));
    g_fifo_ptr = 0u;
    g_rom_page = 0u;
    g_commands = 0u;
}

uint8_t emes_host_read(uint8_t reg) {
    switch (reg & 0x0Fu) {
        case 1u: return g_controller.status;
        case 2u: return g_controller.fifo[g_fifo_ptr++];
        case 3u: return g_controller.error;
        case 12u: return (uint8_t)(g_controller.count >> 8);
        case 13u: return (uint8_t)g_controller.count;
        case 14u: return 0u;
        default: return g_mb[reg & 0x0Fu];
    }
}

void emes_host_write(uint8_t reg, uint8_t value) {
    reg &= 0x0Fu;
    switch (reg) {
        case 0u: {
            const uint8_t command = (uint8_t)(value & 0x7Fu);
            g_rom_page = (uint8_t)(value >> 7);
            if (command != 0u) {
                /* The CoCo wrote the payload at FIFO offsets 0..ptr-1. */
                const uint16_t written = g_fifo_ptr;
                for (unsigned i = 0u; i < 8u; ++i) g_controller.args[i] = g_mb[4u + i];
                g_controller.count = (uint16_t)(((uint16_t)g_mb[12] << 8) | g_mb[13]);
                g_controller.fifo_read = 0u;
                g_controller.fifo_write = written;
                dispatch_command(command);
                ++g_commands;
            }
            g_fifo_ptr = 0u;
            break;
        }
        case 2u:
            g_controller.fifo[g_fifo_ptr++] = value;
            break;
        case 15u:
            if (value != 0u) {
                g_controller.status = base_status();
                g_fifo_ptr = 0u;
                fifo_reset();
            }
            break;
        case 1u:
        case 3u:
        case 14u:
            break;
        default:
            g_mb[reg] = value;
            break;
    }
}

uint8_t emes_host_rom_page(void) { return g_rom_page; }
uint32_t emes_host_commands(void) { return g_commands; }
