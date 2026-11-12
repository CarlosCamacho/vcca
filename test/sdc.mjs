// The CoCoSDC (pak type 13): runs test/sdcrom.raw as the machine's ROM, so no
// Tandy ROM is needed, against an SD card built here, and checks what the
// card did: detection, flash banks, command-mode reads (word and byte mode),
// a write, errors, write protection, mounting, a new image, the directory
// listing, BUSY after a command, and the floppy personality (a WD1793 read).
// node test/sdc.mjs        Every check must pass; the exit code is the count of failures.
import fs from 'fs';
const here = new URL('.', import.meta.url).pathname;
await import(new URL('../src/web/sdcard.js', import.meta.url));

let mem;
const Files = new Map();
const putFile = (name, data, readOnly) => Files.set(name, { data, readOnly: !!readOnly, dirty: false });
const changes = [];
const sd = globalThis.makeSdCard(Files, putFile, () => mem, (...a) => changes.push(a.join(' ')));
const ids = [];
const fileApi = {
	file_open(np, nl, write) {
		const name = new TextDecoder('latin1').decode(new Uint8Array(mem.buffer, np, nl));
		const f = Files.get(name);
		if (!f || (write && f.readOnly)) return -1;
		let id = ids.indexOf(name); if (id < 0) { id = ids.length; ids.push(name); }
		return id;
	},
	file_size: (id) => Files.get(ids[id])?.data.length || 0,
	file_read(id, pos, buf, n) { const f = Files.get(ids[id]); if (!f || pos >= f.data.length) return 0; const c = Math.min(n, f.data.length - pos); new Uint8Array(mem.buffer, buf, c).set(f.data.subarray(pos, pos + c)); return c; },
	file_write(id, pos, buf, n) { const f = Files.get(ids[id]); if (!f || f.readOnly) return 0; if (pos + n > f.data.length) { const g = new Uint8Array(pos + n); g.set(f.data); f.data = g; } f.data.set(new Uint8Array(mem.buffer, buf, n), pos); f.dirty = true; return n; },
	file_close() {},
};

// The SD card.
const disk = (fn) => { const d = new Uint8Array(630 * 256); for (let n = 0; n < 630; n++) for (let i = 0; i < 256; i++) d[n * 256 + i] = fn(n, i) & 255; return d; };
sd.add('STARTUP.CFG', new TextEncoder().encode('0=TEST.DSK\r\n'));
sd.add('TEST.DSK', disk((n, i) => n * 7 + i));
sd.add('GAMES/TWO.DSK', disk((n, i) => n * 3 + i + 0x40));
sd.add('GAMES/TWO.BAK', disk(() => 0x11));   // sorts first: M:GAMES/TWO must add .DSK, not take the first TWO*.*
sd.add('RO.DSK', disk(() => 0xEE), true);

const wasi = { fd_write(fd, iov, n, nw) { const dv = new DataView(mem.buffer); let t = 0; for (let i = 0; i < n; i++) t += dv.getUint32(iov + i * 8 + 4, true); dv.setUint32(nw, t, true); return 0; }, clock_time_get(id, p, o) { new DataView(mem.buffer).setBigUint64(o, 0n, true); return 0; } };
const { instance } = await WebAssembly.instantiate(fs.readFileSync(here + '../build/vcc.wasm'), { wasi_snapshot_preview1: new Proxy(wasi, { get: (t, k) => t[k] || (() => 0) }), vcca: { ...fileApi, ...sd.imports() } });
const x = instance.exports; mem = x.memory; x._initialize();
const put = (b) => { const p = x.vcc_alloc(b.length + 1); new Uint8Array(mem.buffer, p, b.length).set(b); return p; };

let fails = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) fails++; };
const bank = (tag) => { const b = new Uint8Array(16384).fill(0xFF); b.set(new TextEncoder().encode(tag)); return b; };
const hex = (v) => '$' + v.toString(16).toUpperCase().padStart(2, '0');

x.vcc_init();
const rom = fs.readFileSync(here + 'sdcrom.raw');
x.vcc_set_rom(put(rom), rom.length);
x.vcc_multipak(0, 0);
const b0 = bank('SDC0'), b1 = bank('SDC1');
check(x.vcc_insert_cart(0, 13, put(b0), b0.length, put(b1), b1.length) === 1, 'CoCoSDC inserted');
check(x.vcc_insert_cart(1, 1, 0, 0, 0, 0) === 0, 'an FD-502 is refused beside it (one disk controller)');
x.vcc_reset(1);
const mounted0 = new TextDecoder().decode(new Uint8Array(mem.buffer, x.vcc_sdc_mounted(0), 32)).split('\0')[0];
check(mounted0 === 'TEST.DSK', `STARTUP.CFG mounted drive 0 (${mounted0 || 'nothing'})`);
for (let f = 0; f < 120 && x.vcc_peek(0x10ff) !== 0xa5; f++) x.vcc_run_frame();

const rd = (a, n) => Array.from({ length: n }, (_, i) => x.vcc_peek(a + i));
const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const expect = (fn, n) => Array.from({ length: 256 }, (_, i) => fn(n, i) & 255);
check(x.vcc_peek(0x10ff) === 0xa5, 'the test program ran to the end');
check(x.vcc_peek(0x1000) === 0, `the designer's $FF42/$FF43 probe finds the card (${hex(x.vcc_peek(0x1000))})`);
check(String.fromCharCode(...rd(0x1001, 4)) === 'SDC0', `bank 0 at $C000 (${JSON.stringify(String.fromCharCode(...rd(0x1001, 4)))})`);
check(String.fromCharCode(...rd(0x1005, 4)) === 'SDC1', 'bank 1 at $C000 after $FF43 = 1');
check((x.vcc_peek(0x1013) & 1) === 1, `BUSY at once after a command (${hex(x.vcc_peek(0x1013))})`);
const q = (x.vcc_peek(0x1010) << 16) | (x.vcc_peek(0x1011) << 8) | x.vcc_peek(0x1012);
check(q === 630, `'Q': the image has 630 sectors (${q})`);
check(x.vcc_peek(0x1025) === 0, `read LSN 5 finished OK (${hex(x.vcc_peek(0x1025))})`);
check(same(rd(0x1100, 256), expect((n, i) => n * 7 + i, 5)), 'LSN 5, word mode, byte for byte');
check(same(rd(0x1200, 256), expect((n, i) => n * 7 + i, 6)), 'LSN 6, byte mode ($84), byte for byte');
const back = rd(0x1300, 256), wrote = Array.from({ length: 256 }, (_, i) => 255 - i);
check(same(back, wrote), 'LSN 7 reads back what was written');
const img = Files.get('sd:TEST.DSK');
check(same(Array.from(img.data.subarray(7 * 256, 8 * 256)), wrote) && img.dirty, 'the write reached TEST.DSK on the SD card');
check(same(Array.from(img.data.subarray(8 * 256, 9 * 256)), expect((n, i) => n * 7 + i, 8)), 'LSN 8 untouched by a write that did not wait for READY');
check(x.vcc_peek(0x1028) === 0x03, `a block sent before READY loses its first bytes: still BUSY + READY (${hex(x.vcc_peek(0x1028))})`);
check(x.vcc_peek(0x1020) === 0x90, `a read past the end fails with FAILED + $10 (${hex(x.vcc_peek(0x1020))})`);
check(x.vcc_peek(0x1022) === 0, `M:GAMES/TWO mounted drive 1, .DSK added (${hex(x.vcc_peek(0x1022))})`);
check(same(rd(0x1400, 256), expect((n, i) => n * 3 + i + 0x40, 0)), 'drive 1 LSN 0 is TWO.DSK');
check(x.vcc_peek(0x1021) === 0xc0, `a write to read-only RO.DSK fails with FAILED + write protect (${hex(x.vcc_peek(0x1021))})`);
check(x.vcc_peek(0x1023) === 0 && Files.get('sd:NEW.DSK')?.data.length === 161280, `N:NEW.DSK made a 161,280-byte image (${hex(x.vcc_peek(0x1023))})`);
check(x.vcc_peek(0x1024) === 0x90, `X: of a missing file fails with FAILED + not found (${hex(x.vcc_peek(0x1024))})`);
const names = [];
for (let r = 0; r < 16; r++) {
	const rec = rd(0x1500 + r * 16, 16);
	if (!rec[0]) break;
	const n = String.fromCharCode(...rec.slice(0, 8)).trim(), e = String.fromCharCode(...rec.slice(8, 11)).trim();
	names.push((rec[11] & 0x10 ? '/' : '') + n + (e ? '.' + e : ''));
}
check(names.join(' ') === '/GAMES NEW.DSK RO.DSK STARTUP.CFG TEST.DSK', `L:*.* lists the root (${names.join(' ')})`);
check(x.vcc_peek(0x1026) === 1, 'floppy personality: a WD1793 read of 256 bytes, counted, from drive 0');
check(same(rd(0x1600, 256), expect((n, i) => n * 7 + i, 1)), 'floppy track 0 sector 2 is LSN 1');
check((x.vcc_peek(0x1027) & 0x03) === 0, `floppy: BUSY and DRQ clear after the 256th byte (status ${hex(x.vcc_peek(0x1027))})`);
// $04 here is LOST DATA: VCC's WD model ends a sector on a 257th read or a time-out,
// and the time-out sets it. A real WD1773 ends after the 256th byte. Recorded, not asserted.
if (x.vcc_peek(0x1027) & 0x04) console.log('note: the WD model reports LOST DATA after an exact 256-byte read (VCC FD-502 behavior)');

const st = x.vcc_status(); console.log(new TextDecoder().decode(new Uint8Array(mem.buffer, st, 200)).split('\0')[0]);
console.log(fails ? `FAIL: ${fails} checks failed` : 'no errors');
process.exit(fails ? 1 : 0);
