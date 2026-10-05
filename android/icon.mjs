// Draws the launcher icon: dark tile with the CoCo's red/green/blue stripes.
import fs from 'fs'; import zlib from 'zlib';
function png(size, file) {
  const raw = Buffer.alloc((size * 4 + 1) * size);
  const r = size * 0.18;
  for (let y = 0; y < size; y++) {
    raw[y * (size * 4 + 1)] = 0;
    for (let x = 0; x < size; x++) {
      const o = y * (size * 4 + 1) + 1 + x * 4;
      const cx = Math.max(r - x, 0, x - (size - 1 - r)), cy = Math.max(r - y, 0, y - (size - 1 - r));
      const inside = Math.hypot(cx, cy) <= r;
      let c = [28, 30, 34, 255];
      const u = x / size, v = y / size;
      const band = (v - 0.22) / 0.56;
      if (u > 0.16 && u < 0.84 && band >= 0 && band < 1) {
        const i = Math.floor(band * 3);
        const gap = (band * 3) % 1;
        if (gap < 0.8) c = [[224, 52, 46, 255], [76, 175, 80, 255], [52, 120, 220, 255]][i];
      }
      if (!inside) c = [0, 0, 0, 0];
      raw.set(c, o);
    }
  }
  const T = []; for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; T[n] = c >>> 0; }
  const crc = b => { let c = 0xffffffff; for (const v of b) c = T[(c ^ v) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
  const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([l, td, c]); };
  const h = Buffer.alloc(13); h.writeUInt32BE(size, 0); h.writeUInt32BE(size, 4); h[8] = 8; h[9] = 6;
  fs.writeFileSync(file, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', h), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
}
for (const [d, s] of [['mdpi', 48], ['hdpi', 72], ['xhdpi', 96], ['xxhdpi', 144], ['xxxhdpi', 192]]) png(s, `res/mipmap-${d}/ic_launcher.png`);
