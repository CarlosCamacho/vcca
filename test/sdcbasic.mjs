// Disk BASIC 1.1 through the CoCoSDC's floppy personality: the card's flash
// bank 0 holds disk11.rom, its drive 0 a blank image on the SD card. From the
// BASIC prompt: DSKINI0, SAVE a program, NEW, DIR, LOAD it back and RUN it.
// Every step goes through the WD1793 model with the SD card's image behind it.
// node test/sdcbasic.mjs   (coco3.rom and disk11.rom in roms/)
import fs from 'fs';
import { ROMS } from './paths.mjs';
import { check, done } from './check.mjs';
const here = new URL('.', import.meta.url).pathname;
await import(new URL('../src/web/sdcard.js', import.meta.url));

let mem;
const Files = new Map();
const putFile = (name, data, readOnly) => Files.set(name, { data, readOnly: !!readOnly, dirty: false });
const sd = globalThis.makeSdCard(Files, putFile, () => mem, null);
const ids = [];
const fileApi = {
	file_open(np, nl, write) { const name = new TextDecoder('latin1').decode(new Uint8Array(mem.buffer, np, nl)); const f = Files.get(name); if (!f || (write && f.readOnly)) return -1; let id = ids.indexOf(name); if (id < 0) { id = ids.length; ids.push(name); } return id; },
	file_size: (id) => Files.get(ids[id])?.data.length || 0,
	file_read(id, pos, buf, n) { const f = Files.get(ids[id]); if (!f || pos >= f.data.length) return 0; const c = Math.min(n, f.data.length - pos); new Uint8Array(mem.buffer, buf, c).set(f.data.subarray(pos, pos + c)); return c; },
	file_write(id, pos, buf, n) { const f = Files.get(ids[id]); if (!f || f.readOnly) return 0; if (pos + n > f.data.length) { const g = new Uint8Array(pos + n); g.set(f.data); f.data = g; } f.data.set(new Uint8Array(mem.buffer, buf, n), pos); f.dirty = true; return n; },
	file_close() {},
};
sd.add('STARTUP.CFG', new TextEncoder().encode('0=BLANK.DSK\r\n'));
sd.add('BLANK.DSK', new Uint8Array(35 * 18 * 256));

const wasi = { fd_write(fd, iov, n, nw) { const dv = new DataView(mem.buffer); let t = 0; for (let i = 0; i < n; i++) t += dv.getUint32(iov + i * 8 + 4, true); dv.setUint32(nw, t, true); return 0; }, clock_time_get(id, p, o) { new DataView(mem.buffer).setBigUint64(o, 0n, true); return 0; } };
const { instance } = await WebAssembly.instantiate(fs.readFileSync(here + '../build/vcc.wasm'), { wasi_snapshot_preview1: new Proxy(wasi, { get: (t, k) => t[k] || (() => 0) }), vcca: { ...fileApi, ...sd.imports() } });
const x = instance.exports; mem = x.memory; x._initialize();
const put = (b) => { const p = x.vcc_alloc(b.length + 1); new Uint8Array(mem.buffer, p, b.length).set(b); new Uint8Array(mem.buffer)[p + b.length] = 0; return p; };
const strip = (b) => (b.length % 1024 === 2 ? b.subarray(2) : b);
const cstr = (p) => { const u = new Uint8Array(mem.buffer); let e = p; while (u[e]) e++; return new TextDecoder('latin1').decode(u.subarray(p, e)); };

x.vcc_init();
const rom = strip(fs.readFileSync(ROMS + 'coco3.rom'));
x.vcc_set_rom(put(rom), rom.length);
x.vcc_multipak(1, 3);
const disk = strip(fs.readFileSync(ROMS + 'disk11.rom'));
check(x.vcc_insert_cart(3, 13, put(disk), disk.length, 0, 0) === 1, 'CoCoSDC in slot 4 with Disk BASIC 1.1 as its ROM');
x.vcc_turbo_disk(1);
x.vcc_reset(1);
const screen = () => cstr(x.vcc_copy_screen_text());
const frames = (n) => { for (let i = 0; i < n; i++) x.vcc_run_frame(); };
const waitFor = (re, max = 3000) => { for (let i = 0; i < max; i += 10) { frames(10); if (re.test(screen())) return true; } return false; };
const type = (t) => { x.vcc_paste(put(new TextEncoder().encode(t))); };
const tail = () => screen().trim().split('\n').slice(-4).join(' / ');

check(waitFor(/DISK EXTENDED COLOR BASIC[\s\S]*OK/), `Disk BASIC starts from the card's ROM: the CoCo 3 shows its 2.1 banner over disk11.rom (${tail()})`);
type('DSKINI0\r');
check(waitFor(/DSKINI0\s*\nOK/, 20000), `DSKINI0 formats the SD card's image (${tail()})`);
const img = Files.get('sd:BLANK.DSK').data;
check(img[(17 * 18 + 1) * 256] === 0xFF && img[(17 * 18 + 2) * 256] === 0xFF, 'the image on the card now has an empty directory and FAT (track 17)');
type('10 PRINT "SDC OK"\rSAVE"SDCTEST"\r');
check(waitFor(/SAVE"SDCTEST"\s*\nOK/, 6000), `SAVE writes through the floppy emulation (${tail()})`);
type('NEW\rDIR\r');
check(waitFor(/SDCTEST\s+BAS/, 6000), `DIR lists it (${tail()})`);
type('LOAD"SDCTEST"\rRUN\r');
check(waitFor(/RUN\s*\nSDC OK/, 6000), `LOAD and RUN bring it back (${tail()})`);
check(/SDCTEST/.test(new TextDecoder('latin1').decode(img.subarray((17 * 18 + 2) * 256, (17 * 18 + 3) * 256))), 'the file is in the image\'s directory on the SD card');
await done([]);
