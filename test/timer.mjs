// GIME timer in scanline mode: runs test/timerrom.raw (see the .asm) and
// prints the height, in scanlines, of each band of border colour in a frame.
// They must all be equal: the timer counts whole scanlines, so a handler's
// latency never adds up. (Each is N + 2 lines on a 1987 GIME and N + 3 on a
// 1986 one: N + 1 or N + 2 counted from the reload, which this handler,
// running at 0.89 MHz, reaches a scanline after the interrupt.)
// node test/timer.mjs [gime86]   (coco3.rom in roms/)
import fs from 'fs';
import { ROMS } from './paths.mjs';
const gime86 = process.argv[2] === 'gime86';
let mem; const vcca = { file_open: () => -1, file_size: () => 0, file_read: () => 0, file_write: () => 0, file_close() {} };
const wasi = { fd_write(fd, iov, n, nw) { const dv = new DataView(mem.buffer); let t = 0; for (let i = 0; i < n; i++) t += dv.getUint32(iov + i * 8 + 4, true); dv.setUint32(nw, t, true); return 0; }, clock_time_get(id, p, o) { new DataView(mem.buffer).setBigUint64(o, 0n, true); return 0; } };
const here = new URL('.', import.meta.url).pathname;
const { instance } = await WebAssembly.instantiate(fs.readFileSync(here + '../out/vcc.wasm'), { wasi_snapshot_preview1: new Proxy(wasi, { get: (t, k) => t[k] || (() => 0) }), vcca });
const x = instance.exports; mem = x.memory; x._initialize();
const put = (b) => { const p = x.vcc_alloc(b.length + 1); new Uint8Array(mem.buffer, p, b.length).set(b); return p; };
let rom = fs.readFileSync(ROMS + 'coco3.rom'); if (rom.length % 1024 === 2) rom = rom.subarray(2);
x.vcc_init(); x.vcc_set_rom(put(rom), rom.length); x.vcc_multipak(1, 3);
if (x.vcc_gime86) x.vcc_gime86(gime86 ? 1 : 0);
x.vcc_reset(1);
for (let f = 0; f < 150; f++) x.vcc_run_frame();   // to the OK prompt
const code = fs.readFileSync(here + 'timerrom.raw');
for (let i = 0; i < code.length; i++) x.vcc_poke(0x3000 + i, code[i]);
x.vcc_exec(0x3000);
for (let f = 0; f < 10; f++) x.vcc_run_frame();
const px = new Uint32Array(mem.buffer, x.vcc_frame(), 640 * 480);
const runs = []; let last = px[4], n = 0;
for (let y = 0; y < 480; y += 2) { const c = px[y * 640 + 4]; if (c !== last) { runs.push(n); n = 0; last = c; } n++; }
const inner = runs.slice(1);   // the first band starts mid-frame
const heights = [...new Set(inner)].sort((a, b) => a - b);
console.log(`${gime86 ? 'GIME 1986' : 'GIME 1987'}: bands ${inner.join(' ')}`);
console.log(heights.length === 1 ? `PASS: every band ${heights[0]} lines` : `FAIL: band heights vary (${heights.join(', ')})`);
