// Android/WebAssembly port: the parts of VCC's config.h the core uses.
// VCC's config.cpp is the Win32 settings dialogs; port/host.cpp replaces it.
#ifndef __CONFIG_H__
#define __CONFIG_H__
#include "defines.h"
int GetKeyboardLayout();
int GetPaletteType();
enum PALETTETYPE {PALETTE_ORIG=0, PALETTE_UPD=1, PALETTE_NTSC=2};
void GetExtRomPath(char *);
void UpdateTapeCounter(unsigned int,unsigned char,bool force = false);
#endif
