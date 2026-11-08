#ifndef EMESDOS09_HOST_MAILBOX_H
#define EMESDOS09_HOST_MAILBOX_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The controller as the CoCo sees it, for hosts that have no CPLD, SRAM or
 * PIC32: an emulator, or a host test. It wraps the firmware core in main.c
 * behind the 16-byte mailbox at $FF40-$FF4F (offsets 0-15). The CPLD source
 * (emesdos09_revb_top.v) defines the register behavior reproduced here:
 *
 *   0 CTRL    write: bit 7 = ROM page, bits 6-0 = command (0 = none); resets the
 *             FIFO pointer and runs the command to completion.
 *   1 STATUS  read.
 *   2 DATA    FIFO byte; every read or write advances the pointer.
 *   3 ERROR   read.
 *   4-11 ARG0-ARG7, 12-13 COUNT (big endian): plain storage.
 *   14 CAPS   reads 0.
 *   15 ACK    write of a nonzero value acknowledges the result: status returns
 *             to its idle bits and the FIFO pointer resets.
 *
 * The SD card is a raw image the host supplies through emes_host_sd_read and
 * emes_host_sd_write (512-byte sectors). Protocol 2.2 only: a ROM that wants
 * 2.3 falls back, as it does on a real 2.2 controller.
 */
void emes_host_init(void);
uint8_t emes_host_read(uint8_t reg);
void emes_host_write(uint8_t reg, uint8_t value);
/* ROM bank selected by the last CTRL write: 0 or 1. */
uint8_t emes_host_rom_page(void);
/* Number of commands run since init (for tests and status lines). */
uint32_t emes_host_commands(void);

bool emes_host_sd_read(uint32_t lba, uint8_t *sector);
bool emes_host_sd_write(uint32_t lba, const uint8_t *sector);

#endif
