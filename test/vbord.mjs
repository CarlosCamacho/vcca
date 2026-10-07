// GIME VBORD timing: runs test/vbordrom.raw (see the .asm), which changes the
// border colour at each VBORD, and checks that the colour changes exactly
// where the bottom border starts: VBORD fires as the beam leaves the active
// area. (VCC raised it at vertical sync, so the whole frame was one colour.)
// node test/vbord.mjs   (coco3.rom in roms/)
import fs from 'fs';
import { ROMS } from './paths.mjs';
let mem; const vcca = { file_open: () => -1, file_size: () => 0, file_read: () => 0, file_write: () => 0, file_close() {} };
const wasi = { fd_write(fd, iov, n, nw) { const dv = new DataView(mem.buffer); let t = 0; for (let i = 0; i < n; i++) t += dv.getUint32(iov + i * 8 + 4, true); dv.setUint32(nw, t, true); return 0; }, clock_time_get(id, p, o) { new DataView(mem.buffer).setBigUint64(o, 0n, true); return 0; } };
const here = new URL('.', import.meta.url).pathname;
const { instance } = await WebAssembly.instantiate(fs.readFileSync(here + '../out/vcc.wasm'), { wasi_snapshot_preview1: new Proxy(wasi, { get: (t, k) => t[k] || (() => 0) }), vcca });
const x = instance.exports; mem = x.memory; x._initialize();
const put = (b) => { const p = x.vcc_alloc(b.length + 1); new Uint8Array(mem.buffer, p, b.length).set(b); return p; };
let rom = fs.readFileSync(ROMS + 'coco3.rom'); if (rom.length % 1024 === 2) rom = rom.subarray(2);
x.vcc_init(); x.vcc_set_rom(put(rom), rom.length); x.vcc_multipak(1, 3); x.vcc_reset(1);
for (let f = 0; f < 150; f++) x.vcc_run_frame();
const code = fs.readFileSync(here + 'vbordrom.raw');
for (let i = 0; i < code.length; i++) x.vcc_poke(0x3000 + i, code[i]);
x.vcc_exec(0x3000);
for (let f = 0; f < 10; f++) x.vcc_run_frame();
const px = new Uint32Array(mem.buffer, x.vcc_frame(), 640 * 480);
const changes = []; for (let y = 2; y < 480; y += 2) if (px[y * 640 + 4] !== px[(y - 2) * 640 + 4]) changes.push(y / 2);
// 200-line mode: 19 lines of top border, then 199 active lines (VCC's count), then the bottom border.
const bottom = 19 + 199;
console.log(`border colour changes at picture line(s): ${changes.join(', ') || 'none'}; the bottom border starts at ${bottom}`);
console.log(changes.length === 1 && changes[0] === bottom ? 'PASS: VBORD fires as the bottom border begins' : 'FAIL');
