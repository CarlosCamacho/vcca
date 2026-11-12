// CoCoSDC: the SD card controller cartridge (Darren Atkinson), for VCCA.
//
// Command mode is ported from VCC's SDC simulator (sdc/sdc.cpp, EJ Jaquay
// 2025, GPL v3), with the host side replaced: the SD card is a set of files
// the JavaScript front end holds ("sd:" + path), listed and changed through
// the sd_* imports below, not a Windows folder.
//
// Two personalities share $FF40-$FF4B, chosen by the last value written to
// the $FF40 latch:
//   - latch & $43 == $43: command mode. LSN reads and writes of 256-byte
//     sectors, image mounting and the SD card's directories.
//   - anything else: floppy emulation. VCC's SDC answers these itself, with
//     no BUSY, no writes and a DRQ that outlives the 256th byte; VCCA hands
//     them to VCC's FD-502 controller (wd1793.cpp), a WD1793 model, with
//     the SDC's two mounted images as drives 0 and 1. How the real card's
//     firmware emulates the WD1773 is not documented; this behaves like the
//     chip it imitates. See docs/decisions.md.
//
// Differences from VCC's SDC, each a choice (docs/decisions.md):
//   - BUSY is held for a short time after every command (SDC_BUSY_*), so a
//     program that does not wait for the card fails here as it would on one.
//     The real card's timings are not published; these are guesses.
//   - A read past the end of an image, or of a drive with no image, fails
//     with FAILED + $10 (bad LSN / no image); VCC reports $08.
//   - Not ported: 512-byte streaming ($90/$D0), SDF images, writing the
//     flash banks, the Cloud-9 clock.
//
// Register reference: coco-hardware -> references/storage/cocosdc.md.
#include <Windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cctype>

#define HOST_IMPORT(name) __attribute__((import_module("vcca"), import_name(#name)))
extern "C" {
// The SD card's directory "path" ("" is the root) as text, one entry a line:
// "D NAME" or "F SIZE NAME". Returns the length written, or -1 if there is no
// such directory.
HOST_IMPORT(sd_list) int host_sd_list(const char* path, int pathLen, char* out, int cap);
HOST_IMPORT(sd_mkdir) int host_sd_mkdir(const char* path, int pathLen);
HOST_IMPORT(sd_rename) int host_sd_rename(const char* from, int fromLen, const char* to, int toLen);
// 0 deleted, -1 not found, -2 a directory that is not empty.
HOST_IMPORT(sd_delete) int host_sd_delete(const char* path, int pathLen);
HOST_IMPORT(sd_create) int host_sd_create(const char* path, int pathLen, unsigned int size);
}

// The FD-502 controller, used for the floppy personality.
unsigned char disk_io_read(unsigned char port);
void disk_io_write(unsigned char data, unsigned char port);
int mount_disk_image(const char* filename, unsigned char drive);
void unmount_disk_image(unsigned char drive);

namespace
{
	// Status bits in command mode ($FF48 read).
	enum : unsigned char {
		STA_NORMAL = 0x00, STA_BUSY = 0x01, STA_READY = 0x02, STA_INVALID = 0x04,
		STA_ERROR = 0x08, STA_NOTFOUND = 0x10, STA_INUSE = 0x20, STA_WPROTECT = 0x40, STA_FAIL = 0x80,
	};

	// How long, in scanlines (63.5 us), the card answers BUSY after a command.
	const int SDC_BUSY_COMMAND = 2;	// a read, a reply, an extended command: ~0.13 ms
	const int SDC_BUSY_WRITE = 32;	// after a sector is written: ~2 ms
	const int SDC_BUSY_MOUNT = 16;	// mount, directory changes: ~1 ms
	const unsigned MAX_SECTORS = 2097152;

	struct Entry { std::string name; unsigned size = 0; bool dir = false; };

	struct Disk
	{
		HANDLE h = INVALID_HANDLE_VALUE;
		std::string path;	// on the SD card, e.g. "GAMES/BALLOON.DSK"
		std::string name;	// "BALLOON.DSK"
		unsigned size = 0, header = 0;
		bool doubleSided = false, writeProtect = false, raw = false;
		unsigned char record[16] = {};	// the 'I' reply
	};

	Disk gDisk[2];
	std::string gCurDir;	// "" = root
	std::vector<Entry> gFound;	// last search (mount, L:)
	std::string gFoundDir;
	size_t gCursor = 0;
	bool gNextOk = false;

	// The command-mode interface.
	struct Iface
	{
		bool latched = false;
		unsigned char cmd = 0, status = 0, p1 = 0, p2 = 0, p3 = 0, r1 = 0, r2 = 0, r3 = 0;
		bool byteMode = false, receiving = false, half = false;
		unsigned char replyStatus = 0;
		int count = 0;	// bytes left to send or receive
		int pos = 0;
		unsigned char buf[512] = {};
		int busy = 0;	// scanlines of BUSY left
	} I;

	// Flash: eight 16K banks. Only those given ROMs hold anything.
	unsigned char gBank[8][16384];
	unsigned char gCurBank = 0, gFlashData = 0;
	char gStatus[96] = "SDC";

	std::string Upper(std::string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }

	// "A/B/../C" relative to the current directory, or to the root with a leading '/'.
	std::string Resolve(const std::string& p)
	{
		std::vector<std::string> parts;
		auto push = [&](const std::string& s) {
			size_t a = 0;
			while (a <= s.size()) {
				size_t b = s.find('/', a);
				if (b == std::string::npos) b = s.size();
				std::string part = s.substr(a, b - a);
				if (part == "..") { if (!parts.empty()) parts.pop_back(); }
				else if (!part.empty() && part != ".") parts.push_back(Upper(part));
				a = b + 1;
			}
		};
		if (p.empty() || p[0] != '/') push(gCurDir);
		push(p);
		std::string out;
		for (auto& s : parts) out += (out.empty() ? "" : "/") + s;
		return out;
	}

	std::string DirPart(const std::string& path) { size_t s = path.rfind('/'); return s == std::string::npos ? "" : path.substr(0, s); }
	std::string NamePart(const std::string& path) { size_t s = path.rfind('/'); return s == std::string::npos ? path : path.substr(s + 1); }

	// "NAME    EXT" (FAT's 11 characters, no dot) -> "NAME.EXT", as VCC's FixFATPath.
	std::string FixFat(const std::string& p)
	{
		std::string dir = DirPart(p), f = NamePart(p);
		if (f.size() == 11 && f.find('.') == std::string::npos) {
			auto chop = [](std::string s) { size_t sp = s.find(' '); if (sp != std::string::npos) s.erase(sp); return s; };
			std::string n = chop(f.substr(0, 8)), e = chop(f.substr(8, 3));
			f = e.empty() ? n : n + "." + e;
		}
		return dir.empty() ? f : dir + "/" + f;
	}

	bool List(const std::string& dir, std::vector<Entry>& out)
	{
		static char text[65536];
		int n = host_sd_list(dir.c_str(), (int)dir.size(), text, sizeof(text) - 1);
		out.clear();
		if (n < 0) return false;
		text[n] = 0;
		// Not strtok: STARTUP.CFG's parser is mid-strtok when it mounts.
		for (char* line = text; *line; ) {
			char* end = strchr(line, '\n');
			if (end) *end = 0;
			char* next = end ? end + 1 : line + strlen(line);
			Entry e;
			if (line[0] == 'D' && line[1] == ' ') { e.dir = true; e.name = line + 2; }
			else if (line[0] == 'F' && line[1] == ' ') {
				char* sp = strchr(line + 2, ' ');
				if (!sp) continue;
				e.size = (unsigned)strtoul(line + 2, nullptr, 10);
				e.name = sp + 1;
			} else { line = next; continue; }
			out.push_back(e);
			line = next;
		}
		std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.name < b.name; });
		return true;
	}

	bool Match(const char* pat, const char* s)
	{
		for (; *pat; pat++, s++) {
			if (*pat == '*') {
				for (pat++; ; s++) { if (Match(pat, s)) return true; if (!*s) return false; }
			}
			if (!*s) return false;
			if (*pat != '?' && toupper((unsigned char)*pat) != toupper((unsigned char)*s)) return false;
		}
		return !*s;
	}

	// Fills gFound with the entries of "path"'s directory matching its last
	// part, which may hold * and ?. A pattern of * or *.* also lists "..".
	bool Search(const std::string& pattern)
	{
		gFound.clear();
		gCursor = 0;
		gNextOk = false;
		std::string full = Resolve(pattern);
		bool trailing = !pattern.empty() && pattern.back() == '/';
		std::string dir = trailing ? full : DirPart(full);
		std::string pat = trailing ? "*.*" : NamePart(full);
		if (pat.empty()) return false;
		std::vector<Entry> all;
		if (!List(dir, all)) return false;
		bool listing = pat == "*" || pat == "*.*";
		if (listing && !dir.empty()) gFound.push_back({ "..", 0, true });
		for (auto& e : all) {
			bool hit = Match(pat.c_str(), e.name.c_str()) || (pat == "*.*" && e.name.find('.') == std::string::npos);
			if (hit) gFound.push_back(e);
		}
		gFoundDir = dir;
		return !gFound.empty();
	}

	void Record(unsigned char* r, const Entry& e)
	{
		memset(r, ' ', 11);
		std::string n = e.name, x;
		size_t dot = n == ".." ? std::string::npos : n.find('.');
		if (dot != std::string::npos) { x = n.substr(dot + 1); n = n.substr(0, dot); }
		memcpy(r, n.data(), std::min<size_t>(8, n.size()));
		memcpy(r + 8, x.data(), std::min<size_t>(3, x.size()));
		r[11] = (e.dir ? 0x10 : 0);
		r[12] = (unsigned char)(e.size >> 24); r[13] = (unsigned char)(e.size >> 16);
		r[14] = (unsigned char)(e.size >> 8); r[15] = (unsigned char)e.size;
	}

	void Reply(const void* data, int count)
	{
		memcpy(I.buf, data, count);
		I.count = count;
		I.pos = 0;
		I.half = false;
		I.receiving = false;
		I.r2 = I.r3 = 0;
	}

	void Unload(int d)
	{
		if (gDisk[d].h != INVALID_HANDLE_VALUE) CloseHandle(gDisk[d].h);
		gDisk[d] = Disk();
		unmount_disk_image((unsigned char)d);
	}

	// Opens gFound[gCursor] in drive d.
	void OpenFound(int d, bool raw)
	{
		if (gCursor >= gFound.size()) { I.status = STA_FAIL | STA_NOTFOUND; return; }
		Entry e = gFound[gCursor];
		if (e.dir) {
			// A directory mounts the first .DSK inside it, as VCC does.
			std::string sub = gFoundDir.empty() ? e.name : gFoundDir + "/" + e.name;
			if (Search("/" + sub + "/*.DSK")) OpenFound(d, false);
			else I.status = STA_FAIL | STA_NOTFOUND;
			return;
		}
		Unload(d);
		Disk& k = gDisk[d];
		k.path = gFoundDir.empty() ? e.name : gFoundDir + "/" + e.name;
		k.name = e.name;
		k.size = e.size;
		k.raw = raw;
		std::string file = "sd:" + k.path;
		for (int other = 0; other < 2; other++)
			if (other != d && gDisk[other].path == k.path) { k = Disk(); I.status = STA_FAIL | STA_INUSE; return; }
		k.h = CreateFile(file.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		if (k.h == INVALID_HANDLE_VALUE) {
			k.h = CreateFile(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
			k.writeProtect = true;
		}
		if (k.h == INVALID_HANDLE_VALUE) { k = Disk(); I.status = STA_FAIL | STA_NOTFOUND; return; }
		if (!raw) {
			unsigned char head[12] = {};
			DWORD got = 0;
			ReadFile(k.h, head, 12, &got, nullptr);
			if (k.size >= 219262 && !memcmp(head, "SDF1", 4)) { Unload(d); I.status = STA_FAIL | STA_INVALID; return; }
			k.header = k.size & 255;
			switch (k.header) {
			case 0: { unsigned n = k.size >> 8; k.doubleSided = n > 720 && n <= 2880; break; }
			case 1: case 2: case 3: case 4: k.doubleSided = head[1] == 2; break;
			case 12: k.doubleSided = head[8] == 2; if (head[9] & 1) k.writeProtect = true; break;
			default: Unload(d); I.status = STA_FAIL | STA_INVALID; return;
			}
			// The floppy personality reads the same image.
			mount_disk_image(file.c_str(), (unsigned char)d);
		}
		Entry rec = e;
		Record(k.record, rec);
		if (k.writeProtect) k.record[11] |= 0x01;
		gNextOk = gFound.size() > 1;
		I.status = STA_NORMAL;
	}

	void Mount(int d, const std::string& path, bool raw)
	{
		if (path.empty()) { Unload(d); I.status = STA_NORMAL; return; }
		std::string p = FixFat(path);
		bool found = Search(p);
		if (!found && p.find('.') == std::string::npos) {
			found = Search(p + ".DSK");
			if (!found) found = Search(p + "*.*");
		}
		if (!found) { I.status = STA_FAIL | STA_NOTFOUND; return; }
		OpenFound(d, raw);
	}

	// N: / n: a new image. A new .DSK is 35 tracks of 18 sectors, zero-filled.
	void MountNew(int d, const std::string& path, bool raw)
	{
		std::string p = FixFat(path);
		if (Search(p)) { OpenFound(d, raw); return; }
		if (I.p2 != 0) { I.status = STA_FAIL | STA_INVALID; return; }	// SDF: not supported
		std::string full = Resolve(p);
		if (full.empty() || host_sd_create(full.c_str(), (int)full.size(), raw ? 0 : 35 * 18 * 256) != 0) {
			I.status = STA_FAIL | STA_INVALID; return;
		}
		Mount(d, "/" + full, raw);
	}

	void SetDir(const std::string& branch)
	{
		if (branch.empty() || branch == ".") { I.status = STA_NORMAL; return; }
		if (branch == "/") { gCurDir = ""; I.status = STA_NORMAL; return; }
		if (branch.compare(0, 2, "//") == 0) { I.status = STA_FAIL | STA_INVALID; return; }
		std::string full = Resolve(branch);
		std::vector<Entry> tmp;
		if (!List(full, tmp)) { I.status = STA_FAIL | STA_NOTFOUND; return; }
		gCurDir = full;
		I.status = STA_NORMAL;
	}

	bool Seek(int d, unsigned lsn, bool singleSidedLsn)
	{
		Disk& k = gDisk[d];
		if (singleSidedLsn && k.doubleSided) lsn = 2 * 18 * (lsn / 18) + lsn % 18;
		if (lsn > MAX_SECTORS) return false;
		return SetFilePointer(k.h, (LONG)(lsn * 256 + k.header), nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER;
	}

	unsigned Lsn() { return ((unsigned)I.p1 << 16) | ((unsigned)I.p2 << 8) | I.p3; }

	void ReadSector()
	{
		int d = I.cmd & 1;
		Disk& k = gDisk[d];
		I.byteMode = (I.cmd & 4) != 0;
		unsigned lsn = Lsn();
		unsigned char sector[256];
		DWORD got = 0;
		if (k.h == INVALID_HANDLE_VALUE || !Seek(d, lsn, (I.cmd & 2) != 0)
			|| !ReadFile(k.h, sector, 256, &got, nullptr) || got != 256) {
			I.status = STA_FAIL | STA_NOTFOUND;
			return;
		}
		snprintf(gStatus, sizeof(gStatus), "SDC: read %d,%u", d, lsn);
		Reply(sector, 256);
		I.status = STA_NORMAL;
	}

	void WriteSector()
	{
		int d = I.cmd & 1;
		Disk& k = gDisk[d];
		unsigned lsn = Lsn();
		DWORD put = 0;
		if (k.h == INVALID_HANDLE_VALUE) { I.status = STA_FAIL | STA_NOTFOUND; return; }
		if (k.writeProtect) { I.status = STA_FAIL | STA_WPROTECT; return; }
		// Only a raw-mounted image grows; a disk image's size is its geometry.
		if (!k.raw && lsn * 256 + k.header + 256 > k.size) { I.status = STA_FAIL | STA_NOTFOUND; return; }
		if (!Seek(d, lsn, (I.cmd & 2) != 0) || !WriteFile(k.h, I.buf, 256, &put, nullptr) || put != 256) {
			I.status = STA_FAIL | STA_ERROR;
			return;
		}
		if (k.raw && lsn * 256 + 256 > k.size) k.size = lsn * 256 + 256;
		snprintf(gStatus, sizeof(gStatus), "SDC: write %d,%u", d, lsn);
		I.status = STA_NORMAL;
	}

	void LoadDirPage()
	{
		unsigned char page[256] = {};
		for (int n = 0; n < 16 && gCursor < gFound.size(); n++, gCursor++)
			Record(page + n * 16, gFound[gCursor]);
		gNextOk = false;
		I.byteMode = false;
		Reply(page, 256);
		I.status = STA_NORMAL;
	}

	// $C0: no data block; sub-command in param 1.
	void DriveInfo()
	{
		int d = I.cmd & 1;
		I.status = STA_NORMAL;
		switch (I.p1) {
		case 'I':
			if (gDisk[d].h == INVALID_HANDLE_VALUE) { I.status = STA_FAIL | STA_NOTFOUND; break; }
			I.byteMode = false;
			Reply(gDisk[d].record, 16);
			break;
		case 'C': {
			if (gCurDir.empty()) { I.status = STA_FAIL | STA_NOTFOUND; break; }
			unsigned char leaf[32] = {};
			memset(leaf, ' ', 11);
			std::string n = NamePart(gCurDir);
			memcpy(leaf, n.data(), std::min<size_t>(8, n.size()));
			I.byteMode = false;
			Reply(leaf, 32);
			break;
		}
		case 'Q': {
			if (gDisk[d].h == INVALID_HANDLE_VALUE) { I.status = STA_FAIL | STA_NOTFOUND; break; }
			unsigned n = gDisk[d].size / 256;
			I.r1 = (unsigned char)(n >> 16); I.r2 = (unsigned char)(n >> 8); I.r3 = (unsigned char)n;
			I.status = STA_READY;	// as VCC; BUSY is clear, which is what a driver waits for
			break;
		}
		case '>':
			LoadDirPage();
			break;
		case '+':
			if (!gNextOk || gFound.size() < 2) { I.status = STA_FAIL | STA_NOTFOUND; break; }
			gCursor = (gCursor + 1) % gFound.size();
			OpenFound(d, false);
			break;
		case 'V':
			I.r2 = 0x00; I.r3 = 0x01;	// VCC's firmware version
			break;
		default:
			I.status = STA_FAIL | STA_INVALID;
			break;
		}
	}

	// $E0: the 256-byte block received holds "X:" and a path.
	void UpdateSd()
	{
		I.buf[255] = 0;
		const char* arg = (const char*)I.buf + 2;
		int d = I.cmd & 1;
		switch (I.buf[0]) {
		case 'M': Mount(d, arg, false); break;
		case 'm': Mount(d, arg, true); break;
		case 'N': MountNew(d, arg, false); break;
		case 'n': MountNew(d, arg, true); break;
		case 'D': SetDir(arg); break;
		case 'L': {
			std::string p = arg;
			bool ok = Search(p.empty() || p.back() == '/' ? p + "*.*" : p);
			I.status = ok ? STA_NORMAL : STA_FAIL | STA_INVALID;
			break;
		}
		case 'K': {
			std::string full = Resolve(FixFat(arg));
			I.status = (!full.empty() && host_sd_mkdir(full.c_str(), (int)full.size()) == 0) ? STA_NORMAL : STA_FAIL | STA_INVALID;
			break;
		}
		case 'R': {
			std::string from = Resolve(FixFat(arg)), to = Resolve(FixFat(arg + strlen(arg) + 1));
			I.status = host_sd_rename(from.c_str(), (int)from.size(), to.c_str(), (int)to.size()) == 0 ? STA_NORMAL : STA_FAIL | STA_ERROR;
			break;
		}
		case 'X': {
			std::string full = Resolve(FixFat(arg));
			for (auto& k : gDisk) if (k.path == full) { I.status = STA_FAIL | STA_INUSE; return; }
			int r = host_sd_delete(full.c_str(), (int)full.size());
			I.status = r == 0 ? STA_NORMAL : r == -2 ? STA_FAIL | STA_INUSE : STA_FAIL | STA_NOTFOUND;
			break;
		}
		default: I.status = STA_FAIL; break;
		}
	}

	void Command()
	{
		I.busy = SDC_BUSY_COMMAND;
		switch (I.cmd & 0xF0) {
		case 0x80: ReadSector(); break;
		case 0xC0: DriveInfo(); break;
		case 0xA0:
		case 0xE0:
			I.count = 256; I.pos = 0; I.receiving = true; I.half = false;
			I.status = STA_READY | STA_BUSY;
			break;
		default:	// $90 streaming and $D0 control are not ported
			I.status = STA_FAIL | STA_INVALID;
			break;
		}
	}

	void Receive(unsigned char b)
	{
		if (I.count <= 0) return;
		I.buf[I.pos++] = b;
		if (--I.count > 0) return;
		I.receiving = false;
		if ((I.cmd & 0xF0) == 0xA0) { WriteSector(); I.busy = SDC_BUSY_WRITE; }
		else { UpdateSd(); I.busy = SDC_BUSY_MOUNT; }
	}

	unsigned char Send(unsigned char port)
	{
		unsigned char v = 0;
		if (I.byteMode) {
			if (port == 0x4B && I.count > 0) { v = I.buf[I.pos++]; I.count--; }
			return v;
		}
		v = I.buf[I.pos + (port == 0x4B ? 1 : 0)];
		if (I.half) { I.pos += 2; I.count -= 2; I.half = false; }
		else I.half = true;
		return v;
	}

	void ReadStartup()
	{
		HANDLE h = CreateFile("sd:STARTUP.CFG", GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		if (h == INVALID_HANDLE_VALUE) return;
		char text[1024] = {};
		DWORD got = 0;
		ReadFile(h, text, sizeof(text) - 1, &got, nullptr);
		CloseHandle(h);
		for (char* line = strtok(text, "\r\n"); line; line = strtok(nullptr, "\r\n")) {
			if (strlen(line) < 3 || line[1] != '=') continue;
			if (line[0] == '0' || line[0] == '1') Mount(line[0] - '0', line + 2, false);
			else if (line[0] == 'D') SetDir(line + 2);
		}
		I = Iface();
	}
}

// ------------------------------------------------------------- the cartridge

// Power on: ROMs in, both drives empty, then the SD card's STARTUP.CFG.
void SdcPowerOn(const unsigned char* rom, size_t len, const unsigned char* rom2, size_t len2)
{
	memset(gBank, 0xFF, sizeof(gBank));
	if (rom && len) memcpy(gBank[0], rom, std::min<size_t>(len, 16384));
	if (rom2 && len2) memcpy(gBank[1], rom2, std::min<size_t>(len2, 16384));
	for (int d = 0; d < 2; d++) Unload(d);
	for (int d = 2; d < 4; d++) unmount_disk_image((unsigned char)d);
	gCurDir.clear();
	gFound.clear();
	gCurBank = 0;
	gFlashData = 0;
	I = Iface();
	ReadStartup();
	snprintf(gStatus, sizeof(gStatus), "SDC");
}

void SdcPowerOff()
{
	for (int d = 0; d < 2; d++) Unload(d);
	I = Iface();
}

// The CoCo's RESET: the card's interface starts over; mounts stay, as on the card.
void SdcReset()
{
	I = Iface();
	gCurBank = 0;
}

void SdcHSync()
{
	if (I.busy > 0) I.busy--;
}

unsigned char SdcReadMemory(unsigned short address) { return gBank[gCurBank][address & 0x3FFF]; }

unsigned char SdcReadPort(unsigned char port)
{
	if (I.latched) {
		switch (port) {
		case 0x48:
			if (I.busy > 0) return STA_BUSY;
			if (I.count > 0) return I.replyStatus ? I.replyStatus : STA_BUSY | STA_READY;
			return I.status;
		case 0x49: return I.r1;
		case 0x4A: case 0x4B:
			if (I.busy > 0) return 0;
			if (I.count > 0 && !I.receiving) return Send(port);
			return port == 0x4A ? I.r2 : I.r3;
		default: return 0;
		}
	}
	switch (port) {
	case 0x43: return (unsigned char)(gCurBank | (gFlashData & 0xF8));
	case 0x48: case 0x49: case 0x4A: case 0x4B: return disk_io_read(port);
	default: return 0;
	}
}

void SdcWritePort(unsigned char port, unsigned char data)
{
	if (I.latched) {
		switch (port) {
		case 0x40: I = Iface(); break;	// any write leaves command mode
		case 0x48: I.cmd = data; Command(); break;
		case 0x49: I.p1 = data; break;
		// Block bytes sent before the card says READY are lost, as a
		// program that does not wait for READY would lose them.
		case 0x4A: if (I.receiving) { if (!I.busy) Receive(data); } else I.p2 = data; break;
		case 0x4B: if (I.receiving) { if (!I.busy) Receive(data); } else I.p3 = data; break;
		}
		return;
	}
	switch (port) {
	case 0x40:
		if ((data & 0x43) == 0x43) { I = Iface(); I.latched = true; return; }
		disk_io_write(data, port);
		break;
	case 0x42: gFlashData = data; break;
	case 0x43: if (!(data & 0x80)) gCurBank = data & 7; break;	// bit 7 = flash programming: not ported
	case 0x48: case 0x49: case 0x4A: case 0x4B: disk_io_write(data, port); break;
	}
}

void SdcStatus(char* buffer, size_t size)
{
	snprintf(buffer, size, "%s%s 0:%s 1:%s", gStatus, I.latched ? "*" : "",
		gDisk[0].name.empty() ? "-" : gDisk[0].name.c_str(), gDisk[1].name.empty() ? "-" : gDisk[1].name.c_str());
}

// From the front end: mount an SD card file in drive 0 or 1 ("" ejects).
int SdcMount(int drive, const char* path)
{
	if (drive < 0 || drive > 1) return 0;
	Iface saved = I;
	Mount(drive, path && *path ? std::string("/") + path : std::string(), false);
	bool ok = !(I.status & STA_FAIL);
	I = saved;
	return ok ? 1 : 0;
}

const char* SdcMounted(int drive) { return (drive == 0 || drive == 1) ? gDisk[drive].path.c_str() : ""; }
