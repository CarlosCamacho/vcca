// Host side of the Android/WebAssembly port of VCC.
//
// Replaces what Vcc.cpp, config.cpp, Audio.cpp, throttle.cpp, Cassette.cpp
// and DirectDrawInterface.cpp do on Windows: owns SystemState, performs the
// hard/soft reset sequence from Vcc.cpp, hands frames and audio to the
// JavaScript front end, and exports the calls that front end makes.
#include <Windows.h>
#include "defines.h"
#include "coco3.h"
#include "tcc1014graphics.h"
#include "tcc1014registers.h"
#include "tcc1014mmu.h"
#include "mc6821.h"
#include "mc6809.h"
#include "hd6309.h"
#include "keyboard.h"
#include "joystickinput.h"
#include "pakinterface.h"
#include "Cassette.h"
#include "Vcc.h"
#include "config.h"
#include "audio.h"
#include <string>
#include <algorithm>
#include <vector>

#define EXPORT(name) extern "C" __attribute__((export_name(#name)))

// Declared in pak.cpp / vfs.cpp
int PakInsert(int slot, int type, const unsigned char* data, size_t len, const unsigned char* data2, size_t len2);
void PakConfigure(int multiPak, int switchSlot);
int PakMountDisk(int drive, const char* name);
void PakUnmountDisk(int drive);
void PakSetTurboDisk(int on);
void PakStatus(char* buffer, size_t size);

SystemState EmuState;
unsigned char HostCoco3Rom[0x8000];

void (*CPUInit)() = nullptr;
int (*CPUExec)(int) = nullptr;
void (*CPUReset)() = nullptr;
void (*CPUAssertInterupt)(InterruptSource, Interrupt) = nullptr;
void (*CPUDeAssertInterupt)(InterruptSource, Interrupt) = nullptr;
void (*CPUForcePC)(unsigned short) = nullptr;

static const int SurfaceWidth = 640;
static const int SurfaceHeight = 480;
static unsigned int gSurface[SurfaceWidth * SurfaceHeight];	// ARGB, as GimeGpu writes it
static unsigned int gFrame[SurfaceWidth * SurfaceHeight];	// RGBA bytes, for a canvas
static bool gFrameReady = false;
static bool gClsPending = false;
static unsigned int gClsColor = 0;

static const size_t AudioCapacity = 16384;
static unsigned int gAudio[AudioCapacity];
static size_t gAudioCount = 0;

static int gPaletteType = PALETTE_NTSC;
static unsigned char gOverclock = 0;
static std::string gClipboardIn, gClipboardOut;
static char gStatus[256];

//----------------------------------------------------------------------------
// Functions the core expects from config.cpp, Vcc.cpp and friends
//----------------------------------------------------------------------------

int GetKeyboardLayout() { return kKBLayoutNatural; }
int GetPaletteType() { return gPaletteType; }
void GetExtRomPath(char* path) { path[0] = 0; }
void UpdateTapeCounter(unsigned int, unsigned char, bool) {}

static void SetupClock()
{
	int mult = EmuState.OverclockFlag ? EmuState.DoubleSpeedMultiplyer : 2;
	SetClockSpeed(1);
	if (EmuState.DoubleSpeedFlag)
		SetClockSpeed(mult * EmuState.TurboSpeedFlag);
	EmuState.CPUCurrentSpeed = .894;
	if (EmuState.DoubleSpeedFlag)
		EmuState.CPUCurrentSpeed *= (mult * EmuState.TurboSpeedFlag);
}

void SetCPUMultiplyerFlag(unsigned char double_speed)
{
	EmuState.DoubleSpeedFlag = double_speed;
	SetupClock();
}

void SetTurboMode(unsigned char data)
{
	EmuState.TurboSpeedFlag = (data & 1) + 1;
	SetupClock();
}

unsigned char SetCPUMultiplyer(unsigned char multiplyer)
{
	if (multiplyer != QUERY) {
		EmuState.DoubleSpeedMultiplyer = multiplyer;
		SetCPUMultiplyerFlag(EmuState.DoubleSpeedFlag);
	}
	return EmuState.DoubleSpeedMultiplyer;
}

unsigned char SetRamSize(unsigned char size)
{
	if (size != QUERY)
		EmuState.RamSize = size;
	return EmuState.RamSize;
}

unsigned char SetSpeedThrottle(unsigned char throttle)
{
	if (throttle != QUERY)
		EmuState.Throttle = throttle;
	return EmuState.Throttle;
}

unsigned char SetFrameSkip(unsigned char skip)
{
	if (skip != QUERY)
		EmuState.FrameSkip = skip;
	return EmuState.FrameSkip;
}

unsigned char SetCpuType(unsigned char type)
{
	if (type != QUERY)
		EmuState.CpuType = type ? 1 : 0;
	return EmuState.CpuType;
}

unsigned char SetAutoStart(unsigned char) { return 1; }
void DoReboot() { EmuState.ResetPending = 2; }

// throttle.cpp: the browser's animation clock does the pacing.
float CalculateFPS(bool) { return 60.0f; }

// DirectDrawInterface.cpp
int LockScreen()
{
	EmuState.PTRsurface32 = gSurface;
	EmuState.PTRsurface16 = (unsigned short*)gSurface;
	EmuState.PTRsurface8 = (unsigned char*)gSurface;
	EmuState.SurfacePitch = SurfaceWidth;
	EmuState.BitDepth = 3;
	return 0;
}

void UnlockScreen(SystemState*)
{
	for (int i = 0; i < SurfaceWidth * SurfaceHeight; i++) {
		unsigned int p = gSurface[i];
		gFrame[i] = 0xFF000000u | ((p & 0xFF) << 16) | (p & 0xFF00) | ((p >> 16) & 0xFF);
	}
	gFrameReady = true;
}

void Cls(unsigned int color, SystemState* state)
{
	state->ResetPending = 3;
	gClsColor = color;
	gClsPending = true;
}

static void DoCls()
{
	for (int i = 0; i < SurfaceWidth * SurfaceHeight; i++)
		gSurface[i] = gClsColor;
	gGimeGpu.SetBoarderChange();
}

// Audio.cpp: collect each frame's samples for the front end to drain.
void FlushAudioBuffer(unsigned int* buffer, unsigned int bytes)
{
	size_t samples = bytes / 4;
	if (gAudioCount + samples > AudioCapacity)
		gAudioCount = 0;	// the front end stopped draining; drop the backlog
	memcpy(gAudio + gAudioCount, buffer, samples * 4);
	gAudioCount += samples;
}

void ResetAudio() { SetAudioRate(AUDIO_RATE); }	// as Audio.cpp does, via GetTapeRate()
int GetFreeBlockCount() { return 0; }

// coco3.cpp clipboard hooks
std::string HostGetClipboardText() { return gClipboardIn; }
void HostSetClipboardText(const std::string& text) { gClipboardOut = text; }

//----------------------------------------------------------------------------
// Reset sequences, as in Vcc.cpp
//----------------------------------------------------------------------------

static void DoHardReset()
{
	EmuState.RamBuffer = MmuInit(EmuState.RamSize);
	EmuState.WRamBuffer = (unsigned short*)EmuState.RamBuffer;
	if (EmuState.CpuType == 1) {
		CPUInit = HD6309Init;
		CPUExec = HD6309Exec;
		CPUReset = HD6309Reset;
		CPUAssertInterupt = HD6309AssertInterupt;
		CPUDeAssertInterupt = HD6309DeAssertInterupt;
		CPUForcePC = HD6309ForcePC;
	} else {
		CPUInit = MC6809Init;
		CPUExec = MC6809Exec;
		CPUReset = MC6809Reset;
		CPUAssertInterupt = MC6809AssertInterupt;
		CPUDeAssertInterupt = MC6809DeAssertInterupt;
		CPUForcePC = MC6809ForcePC;
	}
	PiaReset();
	mc6883_reset();
	CPUInit();
	CPUReset();
	gGimeGpu.GimeReset();
	GimeRegistersReset();
	MiscReset();
	UpdateBusPointer();
	EmuState.TurboSpeedFlag = 1;
	ResetBus();
	SetCPUMultiplyerFlag(0);
	SetClockSpeed(1);
}

static void SoftReset()
{
	mc6883_reset();
	PiaReset();
	CPUReset();
	gGimeGpu.GimeReset();
	MiscReset();
	MmuReset();
	LoadRom();
	ResetBus();
	SetCPUMultiplyerFlag(0);
	EmuState.TurboSpeedFlag = 1;
}

static void ApplyPendingReset()
{
	switch (EmuState.ResetPending) {
	case 1:
		SoftReset();
		break;
	case 2:
		DoCls();
		DoHardReset();
		break;
	case 3:
	case 4:
		DoCls();
		break;
	default:
		break;
	}
	EmuState.ResetPending = 0;
	gClsPending = false;
}

//----------------------------------------------------------------------------
// Exports for the JavaScript front end
//----------------------------------------------------------------------------

EXPORT(vcc_alloc) void* vcc_alloc(size_t n) { return malloc(n); }
EXPORT(vcc_free) void vcc_free(void* p) { free(p); }

EXPORT(vcc_init) void vcc_init()
{
	EmuState.WindowSize.w = SurfaceWidth;
	EmuState.WindowSize.h = SurfaceHeight;
	EmuState.FrameSkip = 1;
	EmuState.Throttle = 1;
	EmuState.RamSize = _512K;
	EmuState.CpuType = 0;
	EmuState.DoubleSpeedMultiplyer = 2;
	EmuState.ScanLines = 0;
	EmuState.EmulationRunning = 1;
	LockScreen();
	vccKeyboardBuildRuntimeTable(kKBLayoutNatural);
	SetAudioRate(AUDIO_RATE);
	SetSndOutMode(0);
	LeftJS.UseMouse = 0;
	RightJS.UseMouse = 0;
	gGimeGpu.SetPaletteType();
	gGimeGpu.SetMonitorType(1);
	gGimeGpu.SetScanLines(0);
	EmuState.ResetPending = 2;
}

EXPORT(vcc_set_rom) int vcc_set_rom(const unsigned char* data, size_t len)
{
	memset(HostCoco3Rom, 0xFF, sizeof(HostCoco3Rom));
	memcpy(HostCoco3Rom, data, len < sizeof(HostCoco3Rom) ? len : sizeof(HostCoco3Rom));
	return 1;
}

// Cartridge in Multi-Pak slot 0-3. type: 0 empty, 1 FD-502, 2 ROM pak, 3 Orchestra-90,
// 4 Speech/Sound Pak (data = PIC7040 ROM, data2 = SP0256-AL2 ROM), 5 Game Master Cartridge.
EXPORT(vcc_insert_cart) int vcc_insert_cart(int slot, int type, const unsigned char* data, size_t len,
	const unsigned char* data2, size_t len2)
{
	return PakInsert(slot, type, data, len, data2, len2);
}

// multiPak 0: the first occupied slot is plugged straight into the CoCo.
EXPORT(vcc_multipak) void vcc_multipak(int multiPak, int switchSlot) { PakConfigure(multiPak, switchSlot); }

// ram: 0 = 128K, 1 = 512K, 2 = 2M, 3 = 8M. cpu: 0 = 6809, 1 = 6309. Takes effect on hard reset.
EXPORT(vcc_configure) void vcc_configure(int ram, int cpu, int rgb, int scanLines, int overclock)
{
	SetRamSize((unsigned char)ram);
	SetCpuType((unsigned char)cpu);
	gGimeGpu.SetMonitorType(rgb ? 1 : 0);
	EmuState.ScanLines = scanLines ? 1 : 0;
	gGimeGpu.SetBoarderChange();
	EmuState.OverclockFlag = overclock ? 1 : 0;
	EmuState.DoubleSpeedMultiplyer = overclock ? (unsigned char)overclock : 2;
	SetupClock();
}

EXPORT(vcc_reset) void vcc_reset(int hard) { EmuState.ResetPending = hard ? 2 : 1; }

// Emulated-time clock for keyboard.cpp's paste queue: each frame advances it
// 5 ms, so with VCC's 8 ms PasteDelay every paste step lasts two frames.
static LONGLONG gPasteClock = 0;
void HostPasteFrequency(LARGE_INTEGER* f) { f->QuadPart = 1000000000LL; }
void HostPasteClock(LARGE_INTEGER* c) { c->QuadPart = gPasteClock; }

// 1 while VCC wants to run unthrottled (it turns throttling off while pasting).
EXPORT(vcc_unthrottled) int vcc_unthrottled() { return EmuState.Throttle ? 0 : 1; }

// Runs one 60 Hz frame. Returns 1 when a new picture is in the frame buffer.
EXPORT(vcc_run_frame) int vcc_run_frame()
{
	if (EmuState.ResetPending || gClsPending)
		ApplyPendingReset();
	gFrameReady = false;
	gPasteClock += 5000000;
	RenderFrame(&EmuState);
	if (EmuState.ResetPending == 3) {
		EmuState.ResetPending = 0;
		gClsPending = false;
		DoCls();
	}
	return gFrameReady ? 1 : 0;
}

EXPORT(vcc_frame) const unsigned int* vcc_frame() { return gFrame; }
EXPORT(vcc_frame_width) int vcc_frame_width() { return SurfaceWidth; }
EXPORT(vcc_frame_height) int vcc_frame_height() { return SurfaceHeight; }

// Audio: 44.1 kHz stereo, each sample packed as left (low 16) | right (high 16), unsigned.
EXPORT(vcc_audio) const unsigned int* vcc_audio() { return gAudio; }
EXPORT(vcc_audio_count) int vcc_audio_count() { return (int)gAudioCount; }
EXPORT(vcc_audio_clear) void vcc_audio_clear() { gAudioCount = 0; }

// DirectInput scan code (DIK_*), down = 1/0
EXPORT(vcc_key) void vcc_key(int scanCode, int down)
{
	vccKeyboardHandleKey((unsigned char)scanCode, down ? kEventKeyDown : kEventKeyUp);
}

void HostSetStick(int side, unsigned int x, unsigned int y, int button1, int button2);

// Joystick: x, y in 0..63, buttons bit 0 = fire 1, bit 1 = fire 2.
// side: 0 = right stick, 1 = left stick.
EXPORT(vcc_joystick) void vcc_joystick(int side, int x, int y, int buttons)
{
	if (x < 0) x = 0;
	if (x > 63) x = 63;
	if (y < 0) y = 0;
	if (y > 63) y = 63;
	HostSetStick(side, (unsigned int)x << 8, (unsigned int)y << 8, buttons & 1, buttons & 2);
}

EXPORT(vcc_paste) void vcc_paste(const char* text)
{
	gClipboardIn = text ? text : "";
	PasteText();
}

EXPORT(vcc_copy_screen_text) const char* vcc_copy_screen_text()
{
	gClipboardOut.clear();
	CopyText();
	return gClipboardOut.c_str();
}

EXPORT(vcc_mount_disk) int vcc_mount_disk(int drive, const char* name) { return PakMountDisk(drive, name); }
EXPORT(vcc_unmount_disk) void vcc_unmount_disk(int drive) { PakUnmountDisk(drive); }
EXPORT(vcc_turbo_disk) void vcc_turbo_disk(int on) { PakSetTurboDisk(on); }

EXPORT(vcc_status) const char* vcc_status()
{
	char pak[128], tape[48];
	PakStatus(pak, sizeof(pak));
	UpdateTapeStatus(tape, sizeof(tape));
	snprintf(gStatus, sizeof(gStatus), "%s @ %.2f MHz%s%s%s%s",
		EmuState.CpuType ? "HD6309" : "MC6809", EmuState.CPUCurrentSpeed,
		pak[0] ? " | " : "", pak, tape[0] ? " | " : "", tape);
	return gStatus;
}

EXPORT(vcc_peek) int vcc_peek(int address) { return SafeMemRead8((unsigned short)address); }

extern "C" void HostMatrixKey(int col, int row, int down);
// On-screen keyboard: CoCo matrix position (PIA0 column 0-7, row 0-6).
EXPORT(vcc_matrix_key) void vcc_matrix_key(int col, int row, int down) { HostMatrixKey(col, row, down); }

VCC::CPUState MC6809GetState();
EXPORT(vcc_debug_pc) int vcc_debug_pc() { return MC6809GetState().PC; }
EXPORT(vcc_debug_reset_pending) int vcc_debug_reset_pending() { return EmuState.ResetPending; }

extern unsigned int gPortWrites[256];
extern unsigned char gPortLast[256];
EXPORT(vcc_debug_port_writes) int vcc_debug_port_writes(int port) { return (int)gPortWrites[port & 0xFF]; }
EXPORT(vcc_debug_port_last) int vcc_debug_port_last(int port) { return gPortLast[port & 0xFF]; }
extern unsigned short gPortLog[8192];
extern int gPortLogLen;
EXPORT(vcc_debug_log_len) int vcc_debug_log_len() { return gPortLogLen; }
EXPORT(vcc_debug_log) int vcc_debug_log(int i) { return gPortLog[i & 8191]; }

//----------------------------------------------------------------------------
// Hard drive images (IDE) and hi-res joysticks
//----------------------------------------------------------------------------

// Hi-res joystick interface for both ports: 0 off, 2 Tandy Hi-Res Joystick
// Interface, 3 CoCo Max 3 hi-res adapter (VCC's joystickinput.cpp modes).
EXPORT(vcc_hires) void vcc_hires(int type)
{
	LeftJS.HiRes = (unsigned char)type;
	RightJS.HiRes = (unsigned char)type;
}

// Full-resolution stick position, 0..16383, for touch and mouse input.
EXPORT(vcc_joystick_raw) void vcc_joystick_raw(int side, int x, int y, int buttons)
{
	HostSetStick(side, (unsigned int)std::clamp(x, 0, 16383), (unsigned int)std::clamp(y, 0, 16383), buttons & 1, buttons & 2);
}

//----------------------------------------------------------------------------
// Loading programs straight into memory (XRoar's Load / Run)
//----------------------------------------------------------------------------

// Writes through the MMU, as the CPU would: a DECB segment loaded at $FFA2
// sets an MMU register, exactly as LOADM does.
EXPORT(vcc_poke) void vcc_poke(int address, int value) { MemWrite8((unsigned char)value, (unsigned short)address); }

// Starts the CPU at an address, as EXEC does.
EXPORT(vcc_exec) void vcc_exec(int address) { if (CPUForcePC) CPUForcePC((unsigned short)address); }
EXPORT(vcc_debug_log_reset) void vcc_debug_log_reset() { gPortLogLen = 0; }

// Joystick emulation type per port, as VCC's Joystick Configuration sets it:
// 0 standard, 2 Tandy Hi-Res Interface, 3 CoCo Max (CC-MAX).
EXPORT(vcc_hires_ports) void vcc_hires_ports(int left, int right)
{
	LeftJS.HiRes = (unsigned char)left;
	RightJS.HiRes = (unsigned char)right;
}

// BitBanger: VCC's serial printer capture (mc6821.cpp CaptureBit) into the
// front end's file "printer". on = 0 closes it; addLF adds a LF after each CR.
EXPORT(vcc_printer) int vcc_printer(int on, int addLF)
{
	ClosePrintFile();
	SetSerialParams(addLF ? 1 : 0);
	return on ? OpenPrintFile("printer") : 1;
}

// GIME chip: 1 = 1986 (timer fires count + 2 lines after a write), 0 = 1987 (count + 1).
void SetGimeTimer86(int is86);
EXPORT(vcc_gime86) void vcc_gime86(int is86) { SetGimeTimer86(is86); }
