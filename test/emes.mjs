// The emesDOS09 cartridge (pak type 12): the controller firmware core behind the
// $FF40-$FF4F mailbox, with an SD image as a virtual file. Runs test/emesrom.raw
// (see the .asm) from the BASIC prompt and checks what the controller returned:
// the E9CT identity, a mounted FAT16 volume and the root directory of the image.
// node test/emes.mjs [SD-IMAGE [ROM]]   (coco3.rom in roms/)
//
// The image is the emesDOS09 repository's dist/fat16-superfloppy-*.img (default:
// $EMES_DIST or ../emesdos09/dist/ next to this repository). The ROM is not booted:
// on a CoCo 3 the BASIC reset copies the cartridge's $C000-$DFFF into RAM (seen
// here: RAM at $D000, 8,194 cartridge reads at boot), so the ROM's bank-1 resident
// page cannot be read and it stops at "FATAL RAM". See docs/testing.md.
import fs from 'fs';
import path from 'path';
import { ROMS } from './paths.mjs';
const here = new URL('.', import.meta.url).pathname;
const dist = process.env.EMES_DIST || path.join(here, '..', '..', 'emesdos09', 'dist');
const sdPath = process.argv[2] || fs.readdirSync(dist).filter((f) => f.startsWith('fat16-superfloppy')).map((f) => path.join(dist, f))[0];
const romPath = process.argv[3] || fs.readdirSync(dist).filter((f) => f.endsWith('-16K.rom')).map((f) => path.join(dist, f))[0];
if (!sdPath || !romPath) { console.log('FAIL: no SD image or ROM (set EMES_DIST to the emesDOS09 dist/ folder)'); process.exit(1); }

let mem; const files = new Map(); let nextId = 1; const sd = fs.readFileSync(sdPath);
const vcca = {
	file_open(np, nl) { const n = new TextDecoder().decode(new Uint8Array(mem.buffer, np, nl)); if (n !== 'emessd') return -1; const id = nextId++; files.set(id, sd); return id; },
	file_size(id) { return files.get(id).length; },
	file_read(id, pos, buf, n) { const f = files.get(id); const c = Math.max(0, Math.min(n, f.length - pos)); new Uint8Array(mem.buffer, buf, c).set(f.subarray(pos, pos + c)); return c; },
	file_write() { return 0; }, file_close(id) { files.delete(id); },
};
const wasi = { fd_write(fd, iov, n, nw) { const dv = new DataView(mem.buffer); let t = 0; for (let i = 0; i < n; i++) t += dv.getUint32(iov + i * 8 + 4, true); dv.setUint32(nw, t, true); return 0; }, clock_time_get(id, p, o) { new DataView(mem.buffer).setBigUint64(o, 0n, true); return 0; } };
const { instance } = await WebAssembly.instantiate(fs.readFileSync(here + '../build/vcc.wasm'), { wasi_snapshot_preview1: new Proxy(wasi, { get: (t, k) => t[k] || (() => 0) }), vcca: new Proxy(vcca, { get: (t, k) => t[k] || (() => -1) }) });
const x = instance.exports; mem = x.memory; x._initialize();
const put = (b) => { const p = x.vcc_alloc(b.length + 1); new Uint8Array(mem.buffer, p, b.length).set(b); return p; };
let rom = fs.readFileSync(ROMS + 'coco3.rom'); if (rom.length % 1024 === 2) rom = rom.subarray(2);
x.vcc_init(); x.vcc_set_rom(put(rom), rom.length);
const cart = fs.readFileSync(romPath);
x.vcc_multipak(0, 0);
let fails = 0; const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) fails++; };
check(x.vcc_insert_cart(0, 12, put(cart), cart.length, 0, 0) === 1, 'emesDOS09 cartridge inserted');
x.vcc_reset(1);
for (let f = 0; f < 150; f++) x.vcc_run_frame();
const code = fs.readFileSync(here + 'emesrom.raw');
for (let i = 0; i < code.length; i++) x.vcc_poke(0x3000 + i, code[i]);
x.vcc_exec(0x3000);
for (let f = 0; f < 20 && x.vcc_peek(0x3143) !== 0xA5; f++) x.vcc_run_frame();
const rd = (a, n) => Array.from({ length: n }, (_, i) => x.vcc_peek(a + i));
check(x.vcc_peek(0x3143) === 0xA5, 'the test program ran to the end');
check(String.fromCharCode(...rd(0x3100, 4)) === 'E9CT', `IDENTIFY: magic E9CT, protocol ${rd(0x3104, 2).join('.')}`);
check(rd(0x3104, 1)[0] === 2, 'protocol major 2');
check(x.vcc_peek(0x3142) === 0, 'DIR_OPEN of the root succeeded');
const entries = x.vcc_peek(0x3140), ended = x.vcc_peek(0x3141);
check(ended === 0x0F, `DIR_READ ended with NO_MORE_FILES (got ${ended})`);
// The same listing from the image: the C firmware tests print 5 root entries for the rc2 image.
console.log(`root directory: ${entries} entries`);
check(entries > 0 && entries < 100, 'the root directory has entries');
const st = x.vcc_status(); console.log(new TextDecoder().decode(new Uint8Array(mem.buffer, st, 200)).split('\0')[0]);
console.log(fails ? `FAIL: ${fails} checks failed` : 'no errors');
process.exit(fails ? 1 : 0);
