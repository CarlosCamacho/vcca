// node snd2.mjs <frames> <out-prefix>   env: SLOTS="0:5,1:3" (slot:type), SW=switch, HOLD="frame:col:row:len,...", PNGAT="f1,f2"
import fs from 'fs'; import zlib from 'zlib';
import { ROMS as U, TMP as S } from './paths.mjs';
const [framesArg, prefix] = process.argv.slice(2);
let mem; const files = new Map(), ids = [], idOf = new Map();
const fid = n => { if (!idOf.has(n)) { idOf.set(n, ids.length); ids.push(n); } return idOf.get(n); };
const vcca = {
  file_open(p, l, w) { const n = Buffer.from(new Uint8Array(mem.buffer, p, l)).toString('latin1'); return files.has(n) ? fid(n) : -1; },
  file_size(id) { return files.get(ids[id])?.length || 0; },
  file_read(id, pos, buf, n) { const f = files.get(ids[id]); pos >>>= 0; if (!f || pos >= f.length) return 0; const c = Math.min(n >>> 0, f.length - pos); new Uint8Array(mem.buffer, buf, c).set(f.subarray(pos, pos + c)); return c; },
  file_write(id, pos, buf, n) { const f = files.get(ids[id]); f.set(new Uint8Array(mem.buffer, buf, n), pos); return n; },
  file_close() {},
};
const wasi = { fd_write(fd,iov,n,nw){const dv=new DataView(mem.buffer);let t=0;for(let i=0;i<n;i++)t+=dv.getUint32(iov+i*8+4,true);dv.setUint32(nw,t,true);return 0;}, clock_time_get(id,p,o){new DataView(mem.buffer).setBigUint64(o,0n,true);return 0;} };
const { instance } = await WebAssembly.instantiate(fs.readFileSync('../out/vcc.wasm'), { wasi_snapshot_preview1: new Proxy(wasi, { get: (t, k) => t[k] || (() => 0) }), vcca });
const x = instance.exports; mem = x.memory; x._initialize();
const put = b => { const p = x.vcc_alloc(b.length + 1); const u = new Uint8Array(mem.buffer, p, b.length + 1); u.set(b); u[b.length] = 0; return p; };
const strip = b => (b.length % 1024 === 2 ? b.subarray(2) : b);
x.vcc_init(); const r = strip(fs.readFileSync(U + 'coco3.rom')); x.vcc_set_rom(put(r), r.length);
x.vcc_multipak(1, +(process.env.SW ?? 3));
const d = strip(fs.readFileSync(U + 'disk11.rom')); x.vcc_insert_cart(3, 1, put(d), d.length, 0, 0);
for (const s of (process.env.SLOTS || '').split(',').filter(Boolean)) { const [slot, type] = s.split(':').map(Number); x.vcc_insert_cart(slot, type, 0, 0, 0, 0); }
x.vcc_reset(1);
files.set('drive0', new Uint8Array(fs.readFileSync(U + 'balloon.dsk')));
const audio = [];
const segs = (process.env.SEG || '').split(',').filter(Boolean).map(t => { const [name, at, len] = t.split(':'); return { name, at: +at, len: +len }; });
const pngAt = (process.env.PNGAT || '').split(',').filter(Boolean).map(Number);
for (let f = 0; f < +framesArg; f++) {
  if (f === 1) x.vcc_mount_disk(0, put(Buffer.from('drive0')));
  if (f === 120) x.vcc_paste(put(Buffer.from('LOADM"BALLOON":EXEC\r')));
  for (const h of (process.env.HOLD || '').split(',').filter(Boolean)) { const [at, col, row, len] = h.split(':').map(Number); if (f === at) x.vcc_matrix_key(col, row, 1); if (f === at + len) x.vcc_matrix_key(col, row, 0); }
  for (const sg of segs) { if (f === sg.at) sg.w0 = [0x41, 0x7a, 0x7f].map(q => x.vcc_debug_port_writes(q)), sg.a0 = audio.length; if (f === sg.at + sg.len) sg.w1 = [0x41, 0x7a, 0x7f].map(q => x.vcc_debug_port_writes(q)), sg.a1 = audio.length; }
  if (f === +(process.env.LOGAT || -1)) x.vcc_debug_log_reset();
  x.vcc_run_frame();
  const a = new Uint32Array(mem.buffer, x.vcc_audio(), x.vcc_audio_count()); for (const v of a) audio.push(v); x.vcc_audio_clear();
  if (pngAt.includes(f)) png(`${prefix}-${f}.png`);
}
const ports = [0x41, 0x7a, 0x7b, 0x7f].map(p => `$FF${p.toString(16).toUpperCase()}:${x.vcc_debug_port_writes(p)}`);
if (process.env.DUMP) { const n = x.vcc_debug_log_len(), h = new Map(); for (let i = 0; i < Math.min(n, 8192); i++) { const v = x.vcc_debug_log(i); const k = '$FF' + (v >> 8).toString(16).toUpperCase() + '=' + (v & 255).toString(16).padStart(2, '0'); h.set(k, (h.get(k) || 0) + 1); } console.log([...h].sort().map(([k, c]) => k + 'x' + c).join(' ')); }
console.log('pak writes', ports.join(' '));
// audio: per channel, the swing and RMS over the whole run, and over frames after the options menu
const L = audio.map(v => v & 0xffff), R = audio.map(v => v >>> 16);
const stat = (arr) => { const m = arr.reduce((a, b) => a + b, 0) / arr.length; let s = 0, mn = 1e9, mx = -1e9; for (const v of arr) { s += (v - m) ** 2; if (v < mn) mn = v; if (v > mx) mx = v; } return `swing ${mx - mn} rms ${Math.sqrt(s / arr.length).toFixed(0)}`; };
const from = (+(process.env.FROM || 0)) * 735;
for (const sg of segs) { const l = audio.slice(sg.a0, sg.a1); const lv = new Map(); for (const v of l) { const k = (v & 0xffff) >> 7; lv.set(k, (lv.get(k) || 0) + 1); } const ks = [...lv.keys()].sort((a, b) => a - b); console.log(sg.name, 'levels', ks.length, 'min', ks[0], 'max', ks[ks.length - 1]);
  if (process.env.SEGWAV) { const n = l.length, b = Buffer.alloc(44 + n * 2); b.write('RIFF', 0); b.writeUInt32LE(36 + n * 2, 4); b.write('WAVEfmt ', 8); b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(1, 22); b.writeUInt32LE(44100, 24); b.writeUInt32LE(88200, 28); b.writeUInt16LE(2, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(n * 2, 40); const m = l.reduce((a, v) => a + (v & 0xffff), 0) / n; for (let i = 0; i < n; i++) b.writeInt16LE(Math.round((l[i] & 0xffff) - m), 44 + i * 2); fs.writeFileSync(`${process.env.SEGWAV}-${sg.name}.wav`, b); }
  console.log(sg.name.padEnd(12), 'writes GMC', sg.w1[0] - sg.w0[0], 'Orch', sg.w1[1] - sg.w0[1], 'MPI', sg.w1[2] - sg.w0[2], '| L', stat(l.map(v => v & 0xffff)), '| R', stat(l.map(v => v >>> 16))); }
console.log('L', stat(L.slice(from)), '| R', stat(R.slice(from)));
if (process.env.WAV) { const n = audio.length - from, b = Buffer.alloc(44 + n * 4); b.write('RIFF', 0); b.writeUInt32LE(36 + n * 4, 4); b.write('WAVEfmt ', 8); b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(2, 22); b.writeUInt32LE(44100, 24); b.writeUInt32LE(176400, 28); b.writeUInt16LE(4, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(n * 4, 40);
  const mL = L.slice(from).reduce((a, c) => a + c, 0) / n, mR = R.slice(from).reduce((a, c) => a + c, 0) / n;
  for (let i = 0; i < n; i++) { b.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(L[from + i] - mL))), 44 + i * 4); b.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(R[from + i] - mR))), 46 + i * 4); }
  fs.writeFileSync(process.env.WAV, b); }
function png(out) {
  const w = 640, h = 480, px = new Uint8Array(mem.buffer, x.vcc_frame(), w * h * 4), raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) Buffer.from(px.buffer, px.byteOffset + y * w * 4, w * 4).copy(raw, y * (w * 4 + 1) + 1);
  const T = []; for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; T[n] = c >>> 0; }
  const crc = b => { let c = 0xffffffff; for (const v of b) c = T[(c ^ v) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
  const ch = (t, dd) => { const l = Buffer.alloc(4); l.writeUInt32BE(dd.length); const td = Buffer.concat([Buffer.from(t), dd]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([l, td, c]); };
  const ih = Buffer.alloc(13); ih.writeUInt32BE(w, 0); ih.writeUInt32BE(h, 4); ih[8] = 8; ih[9] = 6;
  fs.writeFileSync(out, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), ch('IHDR', ih), ch('IDAT', zlib.deflateSync(raw)), ch('IEND', Buffer.alloc(0))]));
}
