// Android/WebAssembly port: the FD-502 is linked into the emulator rather
// than loaded as a cartridge DLL (port/pak.cpp), so the DLL plumbing is gone.
#pragma once
#include <vcc/util/interrupts.h>
typedef int slot_id_type;
typedef void (*PakAssertInteruptHostCallback)(slot_id_type, Interrupt, InterruptSource);
extern slot_id_type gSlotId;
extern PakAssertInteruptHostCallback AssertInt;
#define External 0
#define TandyDisk 1
#define RGBDisk 2
