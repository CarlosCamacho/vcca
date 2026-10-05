// Cartridge bus for the Android/WebAssembly port.
//
// Desktop VCC loads each cartridge as a DLL behind pakinterface.cpp, and the
// Multi-Pak (mpi.dll) loads up to four more. Here the same cartridges are
// linked in, and a built-in Multi-Pak routes between them using the logic of
// VCC's mpi/multipak_cartridge.cpp:
//   - $FF7F: bits 1-0 SCS slot (disk ports $FF40-$FF5F), bits 5-4 CTS slot (ROM)
//   - other ports go to every slot; reads return the first non-zero answer
//   - cartridge audio is mixed around the 0x80 midpoint, per channel
//
// Cartridges: FD-502 (wd1793.cpp + Disto RTC), ROM pak, Orchestra-90,
// Speech/Sound Cartridge (ssc/ core), Game Master Cartridge (SN76489 + banked ROM).
//
// Parts of this file are ported from MAME's bus/coco drivers, used under the
// BSD-3-Clause license:
//   coco_stecomp.cpp, coco_sym12.cpp   copyright-holders: tim lindner
//   coco_psg.cpp                       copyright-holders: Roberto Fernandez, Nigel Barnes (thanks-to: Ed Snider)
//   coco_ide.cpp                       copyright-holders: Nigel Barnes
//   coco_orch90.cpp (port map only)    copyright-holders: Nathan Woods
//
//   Redistribution and use in source and binary forms, with or without
//   modification, are permitted provided that the following conditions are met:
//   1. Redistributions of source code must retain the above copyright notice,
//      this list of conditions and the following disclaimer.
//   2. Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//   3. Neither the name of the copyright holder nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
//   ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
//   LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
//   CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
//   SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
//   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
//   CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
//   ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
//   POSSIBILITY OF SUCH DAMAGE.
//
// The rest of this file, and the project as a whole, is GPL v3 or later (see LICENSE).
#include <Windows.h>
#include "defines.h"
#include "pakinterface.h"
#include "tcc1014registers.h"
#include "mc6821.h"
#include "fd502/wd1793.h"
#include "fd502/fd502.h"
#include "ssc/ssc_core.h"
#include "gmc/sn76496.h"
#include "ssc/ay8913.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

unsigned char disk_io_read(unsigned char port);
void disk_io_write(unsigned char data, unsigned char port);
void PingFdc();
unsigned short InitController();
int mount_disk_image(const char* filename, unsigned char drive);
void unmount_disk_image(unsigned char drive);
unsigned char SetTurboDisk(unsigned char);
void DiskStatus(char* text_buffer, size_t buffer_size);
unsigned char read_time(unsigned short port);
void write_time(unsigned char data, unsigned char port);

// Globals that fd502.cpp (the DLL shell, not built here) used to define.
slot_id_type gSlotId {};
PakAssertInteruptHostCallback AssertInt = nullptr;
unsigned char PhysicalDriveA = 0, PhysicalDriveB = 0;

static void HostAssertInterrupt(slot_id_type, Interrupt interrupt, InterruptSource)
{
	switch (interrupt) {
	case INT_CART:
		GimeAssertCartInterupt();
		break;
	case INT_NMI:
		CPUAssertInterupt(IS_NMI, INT_NMI);
		break;
	default:
		break;
	}
}

//----------------------------------------------------------------------------
// Cartridges
//----------------------------------------------------------------------------

// Cartridge audio is two unsigned 8-bit channels in one word. VCC's
// GetDACSample (mc6821.cpp) shifts the HIGH byte into the high half of the
// output sample, which is the RIGHT speaker, so the low byte is left.
static unsigned short PackLR(unsigned char left, unsigned char right)
{
	return (unsigned short)((right << 8) | left);
}

struct Cart
{
	virtual ~Cart() = default;
	virtual const char* Name() const = 0;
	virtual void Reset() {}
	virtual void HSync() {}
	virtual unsigned char ReadPort(unsigned char) { return 0; }
	virtual void WritePort(unsigned char, unsigned char) {}
	virtual unsigned char ReadMemory(unsigned short) { return 0; }
	virtual void WriteMemory(unsigned short, unsigned char) {}
	virtual unsigned short SampleAudio() { return 0; }
	// True for carts with a sound output. A sample of 0 is a real level (both
	// channels at the bottom rail), not "no sound", so it cannot be the test.
	virtual bool HasAudio() const { return false; }
	virtual bool CartLine() const { return false; }	// asserts CART (autostart)
	virtual bool IsDisk() const { return false; }
	virtual void Status(char* buffer, size_t size) { snprintf(buffer, size, "%s", Name()); }
};

// FD-502. Only one can exist: wd1793.cpp keeps its state in globals.
struct DiskCart : Cart
{
	std::vector<unsigned char> rom;
	explicit DiskCart(const unsigned char* data, size_t len) : rom(16384, 0xFF)
	{
		if (data && len)
			memcpy(rom.data(), data, std::min<size_t>(len, rom.size()));
		AssertInt = HostAssertInterrupt;
		static bool controllerReady = false;
		if (!controllerReady) {
			InitController();
			controllerReady = true;
		}
	}
	const char* Name() const override { return "FD-502"; }
	bool IsDisk() const override { return true; }
	void HSync() override { PingFdc(); }
	unsigned char ReadPort(unsigned char port) override
	{
		if (port == 0x50 || port == 0x51)
			return read_time(port);
		if (port >= 0x40 && port <= 0x5F)
			return disk_io_read(port);
		return 0;
	}
	void WritePort(unsigned char port, unsigned char data) override
	{
		if (port == 0x50 || port == 0x51)
			write_time(data, port);
		else if (port >= 0x40 && port <= 0x5F)
			disk_io_write(data, port);
	}
	unsigned char ReadMemory(unsigned short address) override { return rom[address & 16383]; }
	void Status(char* buffer, size_t size) override { DiskStatus(buffer, size); }
};

// Program pak, with the bank switching VCC's rom_cartridge does at $FF40.
struct RomCart : Cart
{
	std::vector<unsigned char> rom;
	unsigned int bank = 0;
	RomCart(const unsigned char* data, size_t len) : rom(data, data + len) { if (rom.empty()) rom.assign(1, 0xFF); }
	const char* Name() const override { return "ROM pak"; }
	void Reset() override { bank = 0; }
	void WritePort(unsigned char port, unsigned char data) override
	{
		if (port == 0x40 && rom.size() > 32768)
			bank = (data & 0x0f) << 14;
	}
	unsigned char ReadMemory(unsigned short address) override { return rom[((address & 32767) + bank) % rom.size()]; }
	bool CartLine() const override { return true; }
};

// Orchestra-90 CC: two 8-bit DACs, left at $FF7A and right at $FF7B, as
// MAME's coco_orch90.cpp documents. VCC's orch90.cpp reaches the same
// speakers, though it names $FF7A "RightChannel" (see PackLR).
struct Orch90Cart : Cart
{
	unsigned char rom[8192];
	bool haveRom;
	unsigned char left = 0, right = 0;
	Orch90Cart(const unsigned char* data, size_t len) : haveRom(data && len)
	{
		memset(rom, 0xFF, sizeof(rom));
		if (haveRom)
			memcpy(rom, data, std::min(len, sizeof(rom)));
	}
	const char* Name() const override { return "Orchestra-90"; }
	void WritePort(unsigned char port, unsigned char data) override
	{
		if (port == 0x7A) left = data;
		else if (port == 0x7B) right = data;
	}
	unsigned char ReadMemory(unsigned short address) override { return rom[address & 8191]; }
	bool HasAudio() const override { return true; }
	unsigned short SampleAudio() override { return PackLR(left, right); }
	bool CartLine() const override { return haveRom; }
};

// The cartridges below need no ROM. They are ported from MAME's bus/coco
// drivers (BSD-3-Clause): coco_stecomp.cpp, coco_sym12.cpp and coco_psg.cpp.

static const double CartClockHz = 894886.0;	// E clock, as MAME derives it
static const double SampleSeconds = 1.0 / 44100.0;	// PackAudioSample's rate

static unsigned char ToUnsigned8(int sample16)
{
	return (unsigned char)std::clamp(128 + (sample16 >> 8), 0, 255);
}

// Minimal MC6821, enough for paks that drive chips from its ports. Port A
// has pull-ups, so undriven bits read and output as 1, as in MAME's model.
struct Pia6821
{
	unsigned char ddra = 0, ora = 0, cra = 0, ddrb = 0, orb = 0, crb = 0;
	unsigned char OutA() const { return (unsigned char)((ora & ddra) | (~ddra & 0xFF)); }
	unsigned char OutB() const { return (unsigned char)(orb & ddrb); }
	void Reset() { ddra = ora = cra = ddrb = orb = crb = 0; }
	// Returns 0 (A) or 1 (B) when a port's output changed, else -1.
	int Write(int reg, unsigned char v)
	{
		switch (reg & 3) {
		case 0: if (cra & 4) ora = v; else ddra = v; return 0;
		case 1: cra = v & 0x3F; return -1;
		case 2: if (crb & 4) orb = v; else ddrb = v; return 1;
		default: crb = v & 0x3F; return -1;
		}
	}
	unsigned char Read(int reg, unsigned char inputA) const
	{
		switch (reg & 3) {
		case 0: return (cra & 4) ? (unsigned char)((inputA & ~ddra) | (ora & ddra)) : ddra;
		case 1: return cra;
		case 2: return (crb & 4) ? OutB() : ddrb;
		default: return crb;
		}
	}
};

// Speech Systems Stereo Composer: PIA at $FF70-$FF73, port A drives the
// left 8-bit DAC and port B the right.
struct StereoComposerCart : Cart
{
	Pia6821 pia;
	const char* Name() const override { return "Stereo Composer"; }
	void Reset() override { pia.Reset(); }
	unsigned char ReadPort(unsigned char port) override
	{
		return (port >= 0x70 && port <= 0x73) ? pia.Read(port - 0x70, 0xFF) : 0;
	}
	void WritePort(unsigned char port, unsigned char data) override
	{
		if (port >= 0x70 && port <= 0x73) pia.Write(port - 0x70, data);
	}
	bool HasAudio() const override { return true; }
	unsigned short SampleAudio() override { return PackLR(pia.OutA(), pia.OutB()); }
};

// Speech Systems Symphony 12: PIA at $FF60-$FF63 and four AY-3-8910s.
// Port A is the data bus; port B carries two control bits per chip
// (11 = latch address, 10 = write, 01 = read). Chips 1-2 left, 3-4 right.
struct Symphony12Cart : Cart
{
	Pia6821 pia;
	ssc::Ay8913 ay[4];
	Symphony12Cart() { for (auto& a : ay) a.SetClock(CartClockHz); }
	const char* Name() const override { return "Symphony 12"; }
	void Reset() override { pia.Reset(); for (auto& a : ay) a.Reset(); }
	void Bus(unsigned char bus, unsigned char data)
	{
		for (int i = 0; i < 4; i++) {
			unsigned char ctl = (bus >> (i * 2)) & 3;
			if (ctl == 3) ay[i].SelectRegister(data & 0x0F);
			else if (ctl == 2) ay[i].WriteData(data);
		}
	}
	unsigned char ReadPort(unsigned char port) override
	{
		if (port < 0x60 || port > 0x63) return 0;
		unsigned char in = 0;
		for (int i = 0; i < 4; i++)
			if (((pia.OutB() >> (i * 2)) & 3) == 1) in |= ay[i].ReadData();
		return pia.Read(port - 0x60, in);
	}
	void WritePort(unsigned char port, unsigned char data) override
	{
		if (port < 0x60 || port > 0x63) return;
		int changed = pia.Write(port - 0x60, data);
		if (changed >= 0) Bus(pia.OutB(), pia.OutA());
	}
	bool HasAudio() const override { return true; }
	unsigned short SampleAudio() override
	{
		int s[4];
		for (int i = 0; i < 4; i++) s[i] = ay[i].Render(SampleSeconds);
		return PackLR(ToUnsigned8((s[0] + s[1]) / 2), ToUnsigned8((s[2] + s[3]) / 2));
	}
};

// CoCo PSG (Ed Snider): YM2149 at $FF5E (register) / $FF5F (data), bank
// registers at $FF5A/$FF5B, control at $FF5D, 512K flash and 512K SRAM in
// 8K banks. The flash holds the PSG's own menu firmware, which is optional:
// without it the pak does not autostart, and the sound chip and SRAM still
// work for software loaded from disk. Flash programming is not emulated.
struct CocoPsgCart : Cart
{
	ssc::Ay8913 psg;	// AY-3-8910 core standing in for the YM2149
	std::vector<unsigned char> flash, sram;
	unsigned char bank[2] = {0, 0}, control = 0, selected = 0;
	bool haveFirmware;
	CocoPsgCart(const unsigned char* data, size_t len)
		: flash(0x80000, 0xFF), sram(0x80000, 0), haveFirmware(data && len)
	{
		if (haveFirmware) memcpy(flash.data(), data, std::min(len, flash.size()));
		SetClock();
	}
	void SetClock() { psg.SetClock((control & 1) ? 500000.0 : 1000000.0); }	// bit 0: SEL pin halves the clock
	const char* Name() const override { return "CoCo PSG"; }
	void Reset() override { bank[0] = bank[1] = 0; control = 0; psg.Reset(); SetClock(); }
	unsigned char ReadPort(unsigned char port) override
	{
		switch (port) {
		case 0x5A: case 0x5B: return bank[port & 1];
		case 0x5F: return (selected == 14 || selected == 15) ? 0xFF : psg.ReadData();	// game ports: nothing pressed
		default: return 0;
		}
	}
	void WritePort(unsigned char port, unsigned char data) override
	{
		switch (port) {
		case 0x5A: case 0x5B: bank[port & 1] = data; break;
		case 0x5D: control = data; SetClock(); UpdateCartLineFromSlot(); break;
		case 0x5E: selected = data & 0x0F; psg.SelectRegister(selected); break;
		case 0x5F: psg.WriteData(data); break;
		}
	}
	size_t Offset(unsigned short address) const
	{
		unsigned char b = bank[(address >> 13) & 1];
		return (size_t)(address & 0x1FFF) | ((size_t)(b & 0x3F) << 13);
	}
	unsigned char ReadMemory(unsigned short address) override
	{
		unsigned char b = bank[(address >> 13) & 1];
		return (b & 0x80) ? sram[Offset(address)] : flash[Offset(address)];
	}
	void WriteMemory(unsigned short address, unsigned char data) override
	{
		if ((control & 0x08) && (bank[(address >> 13) & 1] & 0x80))
			sram[Offset(address)] = data;
	}
	bool HasAudio() const override { return true; }
	unsigned short SampleAudio() override
	{
		unsigned char s = ToUnsigned8(psg.Render(SampleSeconds));
		return (unsigned short)((s << 8) | s);
	}
	// Bit 4 of the control register disables autostart; with no firmware
	// there is nothing to start, so CART stays clear.
	bool CartLine() const override { return haveFirmware && !(control & 0x10); }
	static void UpdateCartLineFromSlot();
};

// Glenside-compatible IDE interface. Register map as MAME's coco_ide.cpp:
// eight ATA registers from the base ($FF70, or $FF50 with jumper J2), data
// at +0 with the word's high byte in a latch at +8. The drive logic is VCC's
// SuperIDE/IdeBus.cpp (512-byte sectors, LBA, IDENTIFY), here per instance
// and reading the in-memory files "ide0" (master) and "ide1" (slave).
struct IdeCart : Cart
{
	enum { ATA_ERR = 1, ATA_DRQ = 8, ATA_RDY = 64, ATA_BUSY = 128, ATA_BUSYWAIT = 5 };
	unsigned char base;
	HANDLE disk[2] = { INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE };
	unsigned short idBlock[2][256];
	unsigned char xfer[512];
	unsigned int bufIndex = 0, bufLen = 0, lba = 0;
	unsigned char command = 0, current = 0, latch = 0, busyCounter = 0, select = 0;
	unsigned char secCount = 0, secNumber = 0, cylLo = 0, cylHi = 0, head = 0;
	unsigned char status[2] = { 0, 0 }, error[2] = { 0, 0 };
	char statusText[48] = "IDE: idle";

	explicit IdeCart(unsigned char baseAddress) : base(baseAddress)
	{
		memset(idBlock, 0, sizeof(idBlock));
		for (int d = 0; d < 2; d++) Mount(d);
	}
	~IdeCart() override { for (auto h : disk) if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }

	static void PutString(unsigned short* words, const char* text, int nwords)
	{
		// ATA strings are space-padded with the two bytes of each word swapped.
		char buf[80];
		memset(buf, ' ', sizeof(buf));
		memcpy(buf, text, std::min(strlen(text), (size_t)nwords * 2));
		for (int i = 0; i < nwords; i++)
			words[i] = (unsigned short)(((unsigned char)buf[i * 2] << 8) | (unsigned char)buf[i * 2 + 1]);
	}

	void Mount(int d)
	{
		const char* name = d ? "ide1" : "ide0";
		disk[d] = CreateFile(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (disk[d] == INVALID_HANDLE_VALUE) return;
		unsigned long sectors = GetFileSize(disk[d], nullptr) >> 9;
		unsigned short* id = idBlock[d];
		PutString(&id[10], "VCCA VIRTUAL DISK", 10);
		PutString(&id[23], "1.0", 4);
		PutString(&id[27], "VCCA VIRTUAL IDE DISK", 20);
		id[1] = (unsigned short)(sectors / 0x1000);	// cylinders
		id[3] = 0x0010;	// heads
		id[4] = 0x2000;
		id[5] = 512;
		id[6] = 0x0100;	// sectors per track
		id[20] = 1;
		id[21] = 1;
		id[49] = 0x0200;	// LBA supported
		id[51] = 0x0A00;
		id[54] = (unsigned short)(sectors / 0x1000);
		id[55] = 0x0010;
		id[56] = 0x0100;
		id[60] = (unsigned short)(sectors & 0xFFFF);
		id[61] = (unsigned short)(sectors >> 16);
		status[d] = ATA_RDY;
	}

	const char* Name() const override { return "IDE"; }
	void Reset() override { current = 0; bufIndex = bufLen = 0; latch = 0; }
	void Status(char* buffer, size_t size) override { snprintf(buffer, size, "%s", statusText); }

	void Execute()
	{
		current = command;
		switch (command) {
		case 0x90:	// diagnostics
			for (int d = 0; d < 2; d++) {
				bool present = disk[d] != INVALID_HANDLE_VALUE;
				error[d] = present ? 0x01 : 0;
				status[d] = present ? 0x50 : 0;
			}
			break;
		case 0xEC:	// identify
			if (disk[select] == INVALID_HANDLE_VALUE) { status[select] = ATA_ERR; error[select] = 4; current = 0; break; }
			memcpy(xfer, idBlock[select], 512);
			bufLen = 512; bufIndex = 0;
			status[select] = ATA_DRQ | ATA_RDY;
			busyCounter = ATA_BUSYWAIT;
			break;
		case 0x20: case 0x21: {	// read sectors
			unsigned long got = 0;
			snprintf(statusText, sizeof(statusText), "IDE%d: read %06X", select, lba);
			busyCounter = ATA_BUSYWAIT;
			bufLen = 512; bufIndex = 0;
			status[select] = ATA_DRQ | ATA_RDY;
			memset(xfer, 0, 512);
			if (disk[select] != INVALID_HANDLE_VALUE) {
				SetFilePointer(disk[select], (LONG)(lba * 512), nullptr, FILE_BEGIN);
				ReadFile(disk[select], xfer, 512, &got, nullptr);
			}
			break;
		}
		case 0x30: case 0x31:	// write sectors
			snprintf(statusText, sizeof(statusText), "IDE%d: write %06X", select, lba);
			busyCounter = ATA_BUSYWAIT;
			bufLen = 512; bufIndex = 0;
			status[select] = ATA_DRQ | ATA_RDY;
			memset(xfer, 0, 512);
			break;
		default:	// recalibrate, seek, format track, ...: nothing to do
			current = 0;
			status[select] = ATA_RDY;
			break;
		}
	}

	void RegWrite(int reg, unsigned short data)
	{
		unsigned char b = (unsigned char)data;
		switch (reg) {
		case 0:
			if (!current) return;
			xfer[bufIndex] = data & 0xFF;
			xfer[bufIndex + 1] = data >> 8;
			bufIndex += 2;
			if (bufIndex >= bufLen) {
				if ((current == 0x30 || current == 0x31) && disk[select] != INVALID_HANDLE_VALUE) {
					unsigned long put = 0;
					SetFilePointer(disk[select], (LONG)(lba * 512), nullptr, FILE_BEGIN);
					WriteFile(disk[select], xfer, 512, &put, nullptr);
				}
				bufIndex = bufLen = 0;
				current = 0;
				status[select] = ATA_RDY;
				error[select] = 0;
			}
			break;
		case 2: secCount = b; break;
		case 3: secNumber = b; break;
		case 4: cylLo = b; break;
		case 5: cylHi = b; break;
		case 6: head = b; break;
		case 7: command = b; Execute(); break;
		default: break;
		}
		lba = ((head & 15u) << 24) | ((unsigned)cylHi << 16) | ((unsigned)cylLo << 8) | secNumber;
		select = (head >> 4) & 1;
	}

	unsigned short RegRead(int reg)
	{
		switch (reg) {
		case 0: {
			if (!current) return 0;
			unsigned short v = (unsigned short)(xfer[bufIndex] | (xfer[bufIndex + 1] << 8));
			bufIndex += 2;
			if (bufIndex >= bufLen) {
				bufIndex = bufLen = 0;
				current = 0;
				status[select] = ATA_RDY;
				error[select] = 0;
			}
			return v;
		}
		case 1: return error[select];
		case 2: return secCount;
		case 3: return secNumber;
		case 4: return cylLo;
		case 5: return cylHi;
		case 6: return head;
		case 7:
			if (busyCounter) { busyCounter--; return ATA_BUSY; }
			return status[select];
		default: return 0;
		}
	}

	unsigned char ReadPort(unsigned char port) override
	{
		if (port < base || port > base + 8) return 0;
		int reg = port - base;
		if (reg == 8) return latch;
		unsigned short v = RegRead(reg);
		if (reg == 0) latch = v >> 8;
		return (unsigned char)v;
	}
	void WritePort(unsigned char port, unsigned char data) override
	{
		if (port < base || port > base + 8) return;
		int reg = port - base;
		if (reg == 8) { latch = data; return; }
		RegWrite(reg, reg == 0 ? (unsigned short)((latch << 8) | data) : data);
	}
};

// Speech/Sound Cartridge, as ssc_dll.cpp drives the shared core.
struct SscCart : Cart
{
	ssc::SscCore core;
	bool romsOk;
	SscCart(const unsigned char* pic, size_t picLen, const unsigned char* spo, size_t spoLen)
	{
		core.SetTickRateHz(15720.0);	// ticked from horizontal sync
		bool picOk = pic && picLen >= 4096 && core.LoadPicRom(pic, 4096);
		bool spoOk = spo && spoLen >= 0x800 && core.LoadSpeechRom(spo, 0x800);
		romsOk = picOk && spoOk;
		core.Reset();
	}
	const char* Name() const override { return "Speech/Sound Pak"; }
	void Reset() override { core.Reset(); }
	void HSync() override { if (romsOk) core.Tick(); }
	unsigned char ReadPort(unsigned char port) override { return (port == 0x7D || port == 0x7E) ? core.ReadPort(port) : 0; }
	void WritePort(unsigned char port, unsigned char data) override { if (port == 0x7D || port == 0x7E) core.WritePort(port, data); }
	unsigned char ReadMemory(unsigned short) override { return 0xFF; }
	bool HasAudio() const override { return true; }
	unsigned short SampleAudio() override
	{
		unsigned char s = core.LatchedSample();
		return (unsigned short)((s << 8) | s);
	}
	void Status(char* buffer, size_t size) override
	{
		if (!romsOk) { snprintf(buffer, size, "S/SC: ROMs missing"); return; }
		// Same diagnostics as VCC's ssc_dll.cpp status line.
		snprintf(buffer, size, "S/SC: PC=$%04X cmd=%u spk=%u ALD=%u illeg=%u busy=%s",
			core.CpuPc(), core.CommandCount(), core.AldSpeechCount(), core.AldCount(),
			core.CpuIllegalCount(), core.Busy() ? "1" : "0");
	}
};

// Game Master Cartridge: banked ROM ($FF40) and an SN76489 ($FF41), per GMCCartridge.cpp.
struct GmcCart : Cart
{
	std::vector<unsigned char> rom;
	unsigned char bank = 0;
	SN76489Device psg;
	// With no ROM this is the sound chip alone, for disk games that play
	// through a GMC selected with the Multi-Pak's $FF7F register.
	GmcCart(const unsigned char* data, size_t len) : rom(data, data + len) { psg.device_start(); }
	const char* Name() const override { return "Game Master"; }
	void Reset() override { psg.device_start(); bank = 0; }
	unsigned char ReadPort(unsigned char port) override { return port == 0x40 ? bank : 0; }
	void WritePort(unsigned char port, unsigned char data) override
	{
		if (port == 0x40) bank = data;
		else if (port == 0x41) psg.write(data);
	}
	unsigned char ReadMemory(unsigned short address) override
	{
		if (rom.empty()) return 0;
		return rom[((size_t)bank * 16384 + address) % rom.size()];	// ROM::BankSize is 16K
	}
	bool HasAudio() const override { return true; }
	unsigned short SampleAudio() override
	{
		// sound_stream_update returns one mono level, 0..0x7FFF. Cartridge
		// audio is two packed 8-bit channels around 0x80, so scale it into
		// 0x80..0xFF and send it to both speakers. (Passing the raw word on,
		// as VCC's GMCCartridge::UpdateAudio does, sends its low byte, which
		// changes every sample, to the left speaker as noise.)
		SN76489Device::stream_sample_t l, r;
		unsigned int level = psg.sound_stream_update(l, r);
		// Each of the four channels gets a quarter of the 0..0x7FFF range, so
		// >> 7 lets one channel at full volume reach half of the 8-bit swing.
		unsigned char v = (unsigned char)(0x80 + std::min(level >> 7, 0x7Fu));
		return PackLR(v, v);
	}
	bool CartLine() const override { return !rom.empty(); }
};

//----------------------------------------------------------------------------
// Multi-Pak
//----------------------------------------------------------------------------

static std::unique_ptr<Cart> gSlots[4];
static bool gMultiPak = true;
static unsigned char gSwitchSlot = 3;	// the MPI's front-panel switch, 0-3
static unsigned char gScs = 3, gCts = 3, gSlotRegister = 0;
static bool gTurboDisk = false;

static Cart* Single();
static Cart* Single()
{
	for (auto& s : gSlots)
		if (s) return s.get();
	return nullptr;
}

void UpdateCartLine();
void CocoPsgCart::UpdateCartLineFromSlot() { UpdateCartLine(); }

void UpdateCartLine()
{
	Cart* c = gMultiPak ? gSlots[gScs].get() : Single();
	SetCart(c && c->CartLine());
}

static bool DiskInSlots()
{
	for (auto& s : gSlots)
		if (s && s->IsDisk()) return true;
	return false;
}

// type: 0 empty, 1 FD-502, 2 ROM pak, 3 Orchestra-90, 4 S/SC, 5 GMC,
// 6 Stereo Composer, 7 Symphony 12, 8 CoCo PSG (data = optional firmware),
// 10 IDE at $FF70, 11 IDE at $FF50 (drives are the files "ide0" and "ide1").
// data/len is the cartridge's ROM; for the S/SC, data2/len2 is the SP0256 ROM.
int PakInsert(int slot, int type, const unsigned char* data, size_t len, const unsigned char* data2, size_t len2)
{
	if (slot < 0 || slot > 3)
		return 0;
	gSlots[slot].reset();
	switch (type) {
	case 1:
		if (DiskInSlots()) return 0;	// one FD-502 only
		gSlots[slot] = std::make_unique<DiskCart>(data, len);
		break;
	case 2: gSlots[slot] = std::make_unique<RomCart>(data, len); break;
	case 3: gSlots[slot] = std::make_unique<Orch90Cart>(data, len); break;
	case 4: gSlots[slot] = std::make_unique<SscCart>(data, len, data2, len2); break;
	case 5: gSlots[slot] = std::make_unique<GmcCart>(data, len); break;
	case 6: gSlots[slot] = std::make_unique<StereoComposerCart>(); break;
	case 7: gSlots[slot] = std::make_unique<Symphony12Cart>(); break;
	case 8: gSlots[slot] = std::make_unique<CocoPsgCart>(data, len); break;
	case 10: gSlots[slot] = std::make_unique<IdeCart>(0x70); break;
	case 11: gSlots[slot] = std::make_unique<IdeCart>(0x50); break;
	default: break;
	}
	UpdateCartLine();
	return 1;
}

void PakConfigure(int multiPak, int switchSlot)
{
	gMultiPak = multiPak != 0;
	gSwitchSlot = (unsigned char)(switchSlot & 3);
}

bool PakHasDiskController() { return DiskInSlots(); }
int PakMountDisk(int drive, const char* name) { return mount_disk_image(name, (unsigned char)drive); }
void PakUnmountDisk(int drive) { unmount_disk_image((unsigned char)drive); }
void PakSetTurboDisk(int on) { gTurboDisk = on != 0; SetTurboDisk(on ? 1 : 0); }

void PakStatus(char* buffer, size_t size)
{
	buffer[0] = 0;
	size_t used = 0;
	if (gMultiPak) used = snprintf(buffer, size, "MPI:%d,%d", gCts + 1, gScs + 1);
	for (auto& s : gSlots) {
		if (!s) continue;
		char part[96];
		s->Status(part, sizeof(part));
		if (used < size)
			used += snprintf(buffer + used, size - used, "%s%s", used ? " | " : "", part);
	}
}

void PakTimer()
{
	for (auto& s : gSlots)
		if (s) s->HSync();
}

void ResetBus()
{
	gScs = gCts = gSwitchSlot;
	gSlotRegister = 0b11001100 | gSwitchSlot | (gSwitchSlot << 4);
	for (auto& s : gSlots)
		if (s) s->Reset();
	UpdateCartLine();
}

void UpdateBusPointer() {}

static bool IsDiskPort(unsigned char port) { return port >= 0x40 && port <= 0x5F; }

unsigned char PakReadPort(unsigned char port)
{
	if (!gMultiPak) {
		Cart* c = Single();
		return c ? c->ReadPort(port) : 0;
	}
	if (port == 0x7F) {
		gSlotRegister &= 0b11001100;
		gSlotRegister |= gScs | (gCts << 4);
		return gSlotRegister;
	}
	if (IsDiskPort(port))
		return gSlots[gScs] ? gSlots[gScs]->ReadPort(port) : 0;
	for (auto& s : gSlots) {
		if (!s) continue;
		unsigned char v = s->ReadPort(port);
		if (v) return v;
	}
	return 0;
}

// Diagnostics: per-port write counts and last value, for the test harness.
unsigned int gPortWrites[256];
unsigned char gPortLast[256];
unsigned short gPortLog[8192];
int gPortLogLen = 0;

void PakWritePort(unsigned char port, unsigned char data)
{
	gPortWrites[port]++;
	gPortLast[port] = data;
	if (port == 0x41 || port == 0x7A || port == 0x7F)
		if (gPortLogLen < 8192) gPortLog[gPortLogLen++] = (unsigned short)((port << 8) | data);
	if (!gMultiPak) {
		if (Cart* c = Single()) c->WritePort(port, data);
		return;
	}
	if (port == 0x7F) {
		gScs = data & 3;
		gCts = (data >> 4) & 3;
		gSlotRegister = data;
		UpdateCartLine();
		return;
	}
	if (IsDiskPort(port)) {
		if (gSlots[gScs]) gSlots[gScs]->WritePort(port, data);
		return;
	}
	for (auto& s : gSlots)
		if (s) s->WritePort(port, data);
}

unsigned char PackMem8Read(unsigned short address)
{
	Cart* c = gMultiPak ? gSlots[gCts].get() : Single();
	return c ? c->ReadMemory(address & 32767) : 0;
}

unsigned short PackAudioSample()
{
	const int mid = 0x80;
	int left = 0, right = 0;
	bool any = false;
	for (auto& s : gSlots) {
		if (!s) continue;
		if (!s->HasAudio()) continue;
		unsigned short sample = s->SampleAudio();
		any = true;
		left += ((sample >> 8) & 0xFF) - mid;
		right += (sample & 0xFF) - mid;
	}
	if (!any) return 0;
	left = std::clamp(left + mid, 0, 255);
	right = std::clamp(right + mid, 0, 255);
	return (unsigned short)((left << 8) | right);
}

void PackMem8Write(unsigned short address, unsigned char data)
{
	Cart* c = gMultiPak ? gSlots[gCts].get() : Single();
	if (c) c->WriteMemory(address & 32767, data);
}
