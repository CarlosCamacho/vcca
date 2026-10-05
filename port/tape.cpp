// Cassette deck for the Android/WebAssembly port.
//
// A port of VCC's Cassette.cpp (Copyright 2015 Joseph Forgione, GPL v3),
// with its file dialogs and Win32 handles replaced by the JavaScript file
// "tape". The front end hands over two kinds of tape:
//
//   TAPE_CAS  a .cas byte stream, held here in CasBuffer exactly as VCC does
//   TAPE_WAV  any .wav, which the front end has already converted to 8-bit
//             unsigned mono at 44.1 kHz, so it is read straight through
//
// Converting in JavaScript means every WAV, whatever its rate, width or
// channel count, plays at the 44.1 kHz the audio path runs at.
#include <Windows.h>
#include <string.h>
#include <stdio.h>
#include <algorithm>
#include "defines.h"
#include "coco3.h"
#include "config.h"
#include "Cassette.h"

#define HOST_IMPORT(name) __attribute__((import_module("vcca"), import_name(#name)))
#define EXPORT(name) extern "C" __attribute__((export_name(#name)))
extern "C" {
HOST_IMPORT(file_open) int host_file_open(const char* name, int nameLen, int write);
HOST_IMPORT(file_size) unsigned int host_file_size(int id);
HOST_IMPORT(file_read) unsigned int host_file_read(int id, unsigned int pos, void* buf, unsigned int n);
HOST_IMPORT(file_write) unsigned int host_file_write(int id, unsigned int pos, const void* buf, unsigned int n);
}

static unsigned char MotorState = 0, TapeMode = STOP, Quiet = 30;
static int TapeFile = -1;
static unsigned long TapeOffset = 0, TotalSize = 0;
static unsigned char TempBuffer[8192];
static unsigned char CasBuffer[CAS_WRITEBUFFERSIZE];
static bool TapeWritten = false;
static unsigned char FileType = TAPE_UNKNOWN;
static unsigned char TapeFastLoad = 1;
static unsigned int TempIndex = 0;
static unsigned int MotorOffDelay = 0;

static unsigned char One[21] = { 0xC8,0xE8,0xE8,0xF8,0xF8,0xE8,0xC8,0xA8,0x78,0x50,0x50,0x30,0x10,0x00,0x00,0x10,0x30,0x30,0x50,0x80,0xA8 };
static unsigned char Zero[40] = { 0xC8,0xD8,0xE8,0xE8,0xF0,0xF8,0xF8,0xF8,0xF0,0xE8,0xD8,0xC8,0xB8,0xA8,0x90,0x78,0x78,0x68,0x50,0x40,0x30,0x20,0x10,0x08,0x00,0x00,0x00,0x08,0x10,0x10,0x20,0x30,0x40,0x50,0x68,0x68,0x80,0x90,0xA8,0xB8 };

static bool InitWaveforms()
{
	// reduce peak to peak to 75% for .cas as its more realistic what actual tape uses
	for (size_t i = 0; i < sizeof(One); ++i)
		One[i] = One[i] / 2 + One[i] / 4 + 64 / 2;
	for (size_t i = 0; i < sizeof(Zero); ++i)
		Zero[i] = Zero[i] / 2 + Zero[i] / 4 + 64 / 2;
	return true;
}
static bool gInit = InitWaveforms();

// Write state
static int LastTrans = 0;
static unsigned char Mask = 0, Byte = 0, LastSample = 0;

static void WavtoCas(const unsigned char*, unsigned int);
static void CastoWav(unsigned char*, unsigned int);
static void SyncFileBuffer();

unsigned int GetTapeRate() { return CAS_TAPEAUDIORATE; }

unsigned char GetMotorState()
{
	if (MotorOffDelay > 0)
	{
		--MotorOffDelay;
		return 1;
	}
	return MotorState;
}

bool IsTapeWav() { return TapeFile >= 0 && FileType == TAPE_WAV; }

bool GetTapePlaybackFastLoad()
{
	return !IsTapeWav() && TapeFastLoad && TapeMode == PLAY && TotalSize > 0;
}

void Motor(unsigned char State)
{
	MotorState = State;
	if (!MotorState)
	{
		SetSndOutMode(0);
		if (TapeMode == PLAY)
		{
			Quiet = 15;
			TempIndex = 0;
			MotorOffDelay = 10;
		}
		else if (TapeMode == REC)
			SyncFileBuffer();
		return;
	}
	SetSndOutMode(TapeMode == PLAY ? 2 : TapeMode == REC ? 1 : 0);
}

unsigned int GetTapeCounter() { return TapeOffset; }

void SetTapeCounter(unsigned int Count, bool)
{
	TapeOffset = Count;
	if (TapeOffset > TotalSize)
		TotalSize = TapeOffset;
}

void UpdateTapeStatus(char* status, int max)
{
	if (max > 0) status[0] = 0;
	if (TapeFile < 0) return;
	const char* mode = TapeMode == PLAY ? "Play" : TapeMode == REC ? "Rec" : "Tape";
	if (TotalSize > 0)
		snprintf(status, max, "%s %lu%%%s", mode,
			(unsigned long)(std::min(TotalSize, TapeOffset) * 100 / TotalSize), MotorState ? " *" : "");
	else
		snprintf(status, max, "%s%s", mode, MotorState ? " *" : "");
}

void SetTapeMode(unsigned char Mode)
{
	if (TapeMode == REC && Mode != REC)
		SyncFileBuffer();
	TapeMode = Mode;
	if ((Mode == PLAY || Mode == REC) && TapeFile < 0)
		TapeMode = STOP;
	if (MotorState)
		Motor(1);
	else
		SetSndOutMode(0);
}

void FlushCassetteBuffer(const unsigned char* Buffer, unsigned int* Len)
{
	if (TapeMode != REC)
		return;

	unsigned int Length = *Len;
	*Len = 0;

	TapeWritten = true;
	switch (FileType)
	{
	case TAPE_WAV:
		TapeOffset += host_file_write(TapeFile, TapeOffset, Buffer, Length);
		if (TapeOffset > TotalSize)
			TotalSize = TapeOffset;
		break;

	case TAPE_CAS:
		WavtoCas(Buffer, Length);
		break;
	}
}

void LoadCassetteBuffer(unsigned char* CassBuffer, unsigned int* CassBufferSize)
{
	if (TapeMode != PLAY)
	{
		*CassBufferSize = CAS_TAPEAUDIORATE / 60;
		memset(&CassBuffer[0], CAS_SILENCE, *CassBufferSize);
		return;
	}

	switch (FileType)
	{
	case TAPE_WAV:
	{
		unsigned int got = host_file_read(TapeFile, TapeOffset, CassBuffer, CAS_TAPEAUDIORATE / 60);
		if (got == 0)
		{
			TapeMode = STOP;	// end of tape
			got = CAS_TAPEAUDIORATE / 60;
			memset(CassBuffer, CAS_SILENCE, got);
		}
		TapeOffset += got;
		if (TapeOffset > TotalSize)
			TapeOffset = TotalSize;
		*CassBufferSize = got;
		break;
	}

	case TAPE_CAS:
		CastoWav(CassBuffer, CAS_TAPEREADAHEAD);
		*CassBufferSize = CAS_TAPEREADAHEAD;
		break;

	default:
		*CassBufferSize = CAS_TAPEAUDIORATE / 60;
		memset(&CassBuffer[0], CAS_SILENCE, *CassBufferSize);
		break;
	}
}

static void SyncFileBuffer()
{
	if (!TapeWritten || TapeFile < 0) return;
	if (FileType == TAPE_CAS)
	{
		CasBuffer[TapeOffset] = Byte;	// capture the last byte
		LastTrans = 0;	// reset all static inter-call variables
		Mask = 0;
		Byte = 0;
		LastSample = 0;
		TempIndex = 0;
		host_file_write(TapeFile, 0, CasBuffer, TapeOffset);
	}
	TapeWritten = false;
}

static void CastoWav(unsigned char* Buffer, unsigned int BytestoConvert)
{
	unsigned char Byte = 0;
	char Mask = 0;

	// copy any left over bytes and fill remaining space with silence
	auto fillSilence = [&]()
	{
		int remaining = TempIndex - BytestoConvert;
		if (TempIndex)
			memcpy(Buffer, TempBuffer, TempIndex);
		memset(&Buffer[TempIndex], CAS_SILENCE, -remaining);
		TempIndex = 0;
	};

	if (Quiet > 0)
	{
		Quiet--;
		fillSilence();
		return;
	}

	if (TapeOffset >= TotalSize || TotalSize == 0)	// End of tape return nothing
	{
		TapeMode = STOP;	// Stop at end of tape
		fillSilence();
		return;
	}

	while (TempIndex < BytestoConvert && TapeOffset < TotalSize)
	{
		Byte = CasBuffer[(TapeOffset++) % TotalSize];
		if (GetTapePlaybackFastLoad())
		{
			for (Mask = 0; Mask <= 7; ++Mask, Byte >>= 1)
			{
				// color basic expects high/low transitions so this
				// is the smallest waveform that we can have without
				// hacking the rom. CA
				// high/low waveform (b1) + tape bit (b0)
				TempBuffer[TempIndex++] = 2 + (Byte & 1);
				TempBuffer[TempIndex++] = 0 + (Byte & 1);
			}
		}
		else
		{
			for (Mask = 0; Mask <= 7; Mask++)
			{
				if ((Byte & (1 << Mask)) == 0)
				{
					memcpy(&TempBuffer[TempIndex], Zero, 40);
					TempIndex += 40;
				}
				else
				{
					memcpy(&TempBuffer[TempIndex], One, 21);
					TempIndex += 21;
				}
			}
		}
	}

	int remaining = TempIndex - BytestoConvert;
	if (remaining >= 0)
	{
		memcpy(Buffer, TempBuffer, BytestoConvert);
		if (remaining > 0)
			memmove(TempBuffer, &TempBuffer[BytestoConvert], remaining);
		TempIndex -= BytestoConvert;
		return;
	}

	fillSilence();
}

static void WavtoCas(const unsigned char* WaveBuffer, unsigned int Length)
{
	unsigned char Bit = 0, Sample = 0;
	unsigned int Index = 0, Width = 0;

	for (Index = 0; Index < Length; Index++)
	{
		Sample = WaveBuffer[Index];
		if ((LastSample <= 0x80) & (Sample > 0x80))	// Low to High transition
		{
			Width = Index - LastTrans;
			if ((Width < 10) | (Width > 50))	// Invalid Sample Skip it
			{
				LastSample = 0;
				LastTrans = Index;
				Mask = 0;
				Byte = 0;
			}
			else
			{
				Bit = 1;
				if (Width > 30)
					Bit = 0;
				Byte = Byte | (Bit << Mask);
				Mask++;
				Mask &= 7;
				if (Mask == 0)
				{
					CasBuffer[TapeOffset++] = Byte;
					Byte = 0;
					if (TapeOffset >= CAS_WRITEBUFFERSIZE - 1)	// Don't blow past the end of the buffer
						TapeMode = STOP;
				}
			}
			LastTrans = Index;
		}
		LastSample = Sample;
	}
	LastTrans -= Length;
	if (TapeOffset > TotalSize)
		TotalSize = TapeOffset;
}

//----------------------------------------------------------------------------
// Front-end controls

// Opens the front end's file "tape". kind: TAPE_CAS or TAPE_WAV (already 8-bit
// mono 44.1 kHz). Returns 1 on success.
EXPORT(vcc_tape_insert) int vcc_tape_insert(int kind)
{
	if (TapeMode == REC) SyncFileBuffer();
	TapeMode = STOP;
	SetSndOutMode(0);
	TapeFile = host_file_open("tape", 4, 1);
	if (TapeFile < 0)
		TapeFile = host_file_open("tape", 4, 0);
	if (TapeFile < 0) { FileType = TAPE_UNKNOWN; TotalSize = 0; return 0; }
	FileType = (unsigned char)kind;
	TotalSize = host_file_size(TapeFile);
	TapeOffset = 0;
	TempIndex = 0;
	Quiet = 30;
	LastTrans = 0; Mask = 0; Byte = 0; LastSample = 0;
	TapeWritten = false;
	if (FileType == TAPE_CAS)
	{
		if (TotalSize > CAS_WRITEBUFFERSIZE - 1)
			TotalSize = CAS_WRITEBUFFERSIZE - 1;
		memset(CasBuffer, 0, sizeof(CasBuffer));
		host_file_read(TapeFile, 0, CasBuffer, TotalSize);
	}
	return 1;
}

EXPORT(vcc_tape_eject) void vcc_tape_eject()
{
	if (TapeMode == REC) SyncFileBuffer();
	TapeMode = STOP;
	SetSndOutMode(0);
	TapeFile = -1;
	FileType = TAPE_UNKNOWN;
	TotalSize = TapeOffset = 0;
}

// mode: 0 stop, 1 play, 2 record
EXPORT(vcc_tape_mode) void vcc_tape_mode(int mode) { SetTapeMode((unsigned char)mode); }

// Moves the tape to a byte position (0 rewinds). Recording from here on
// overwrites what follows, as on a real recorder.
EXPORT(vcc_tape_seek) void vcc_tape_seek(unsigned int offset)
{
	if (TapeMode == REC) SyncFileBuffer();
	if (offset > TotalSize) offset = TotalSize;
	TapeOffset = offset;
	TempIndex = 0;
	Quiet = 15;
	LastTrans = 0; Mask = 0; Byte = 0; LastSample = 0;
}

EXPORT(vcc_tape_fastload) void vcc_tape_fastload(int on) { TapeFastLoad = on ? 1 : 0; }

// Writes anything recorded but not yet in the file "tape".
EXPORT(vcc_tape_sync) void vcc_tape_sync()
{
	if (TapeWritten && TapeFile >= 0 && FileType == TAPE_CAS)
		host_file_write(TapeFile, 0, CasBuffer, TapeOffset);	// a byte still being assembled waits
}

// [mode, motor, offset, total, kind]
EXPORT(vcc_tape_info) const unsigned int* vcc_tape_info()
{
	static unsigned int info[5];
	info[0] = TapeFile < 0 ? 3 : TapeMode;
	info[1] = MotorState;
	info[2] = TapeOffset;
	info[3] = TotalSize;
	info[4] = TapeFile < 0 ? 0 : FileType;
	return info;
}
