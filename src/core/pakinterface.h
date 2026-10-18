// Android/WebAssembly port: cartridge bus. VCC loads cartridges as DLLs; here
// the FD-502 disk controller and plain ROM paks are linked in (port/pak.cpp).
#pragma once
void PakTimer();
unsigned char PakReadPort (unsigned char);
void PakWritePort(unsigned char,unsigned char);
unsigned char PackMem8Read (unsigned short);
void PackMem8Write(unsigned short, unsigned char);
unsigned short PackAudioSample();
void ResetBus();
void UpdateBusPointer();
