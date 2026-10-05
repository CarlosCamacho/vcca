import { chromium } from 'playwright';
import fs from 'fs';
import { ROMS as U, TMP as S } from './paths.mjs';
const b = await chromium.launch();
const ctx = await b.newContext({ acceptDownloads: true });
const page = await ctx.newPage(); const logs = []; page.on('pageerror', e => logs.push(e.message));
page.on('dialog', d => d.accept());
await page.goto('http://127.0.0.1:8765/index.html'); await page.waitForTimeout(800);
async function pick(sel, file) { const [fc] = await Promise.all([page.waitForEvent('filechooser'), page.evaluate((s) => document.querySelector(s).click(), sel)]); await fc.setFiles(file); await page.waitForTimeout(600); }
await pick('#wDisk', U + 'disk11.rom');
await pick('#wRom', U + 'coco3.rom');
const screen = () => page.evaluate(() => cstr(vcc.vcc_copy_screen_text()));
const type = (t) => page.evaluate((t) => typeText(t), t);
async function waitFor(re, ms = 60000) { const t0 = Date.now(); while (Date.now() - t0 < ms) { const s = await screen(); if (re.test(s)) return s; await page.waitForTimeout(250); } return 'TIMEOUT\n' + await screen(); }
const last = (s) => s.trim().split('\n').slice(-6).join(' / ');
await waitFor(/OK/);
const fast = process.argv[2] === 'slow' ? 0 : 1;
await page.evaluate((f) => { settings.tapeFast = f; applyConfig(); }, fast);
console.log('mode', fast ? 'fast' : 'real time');

// 1. Record a BASIC program onto a blank tape
await page.evaluate(() => newBlankTape());
await type('10 PRINT "TAPE OK"\r');
await page.waitForTimeout(500);
await page.evaluate(() => tapeControl(2));
let t0 = Date.now();
await type('CSAVE"HELLO"\r'); await page.waitForTimeout(1000);
console.log('csave:', last(await waitFor(/CSAVE"HELLO"\s*\n\s*OK/)), (Date.now() - t0) + 'ms');
await page.evaluate(() => tapeControl(0));
const rec = await page.evaluate(() => { const f = Files.get('tape'); return { size: f.data.length, first: tapeFirstFile(TAPE_CAS, f.data), info: tapeInfo() }; });
console.log('recorded', JSON.stringify(rec));

// 2. ML too, appended after the BASIC file
await page.evaluate(() => tapeControl(2));
await type('POKE&H3000,&H86:POKE&H3001,&H2A:POKE&H3002,&HB7:POKE&H3003,4:POKE&H3004,0:POKE&H3005,&H39\r'); await page.waitForTimeout(800);
await type('CSAVEM"ML",&H3000,&H3005,&H3000\r'); await page.waitForTimeout(1000);
console.log('csavem:', last(await waitFor(/CSAVEM.*\n\s*OK/)));
await page.evaluate(() => tapeControl(0));
const cas = await page.evaluate(() => Array.from(Files.get('tape').data));
fs.writeFileSync(S + 'hello.cas', Buffer.from(cas));
console.log('cas bytes', cas.length);

// 3. Rewind, NEW, CLOAD, RUN
await page.evaluate(() => { tapeRewind(); tapeControl(1); });
await type('NEW\r'); await page.waitForTimeout(500);
t0 = Date.now();
await type('CLOAD\r'); await page.waitForTimeout(500);
console.log('cload:', last(await waitFor(/CLOAD[\s\S]*OK\s*$/)), (Date.now() - t0) + 'ms');
await type('RUN\r');
console.log('run:', last(await waitFor(/TAPE OK/)));
// 4. CLOADM continues from where the tape is
await type('POKE&H400,96:CLOADM:EXEC\r');
await waitFor(/EXEC[\s\S]*OK\s*$/);
console.log('cloadm+exec: $400 =', await page.evaluate(() => vcc.vcc_peek(0x400).toString(16)), '(2a expected)');

// 5. A WAV made from the CAS (22050 Hz, 16-bit stereo), through Run
function casToWav(bytes, rate) {
  const s = []; for (let i = 0; i < rate / 2; i++) s.push(0);
  for (const byte of bytes) for (let bit = 0; bit < 8; bit++) {
    const f = (byte >> bit) & 1 ? 2400 : 1200, n = Math.round(rate / f);
    for (let i = 0; i < n; i++) s.push(Math.sin(2 * Math.PI * i / n) * 0.7);
  }
  for (let i = 0; i < rate / 2; i++) s.push(0);
  const out = Buffer.alloc(44 + s.length * 4);
  out.write('RIFF', 0); out.writeUInt32LE(36 + s.length * 4, 4); out.write('WAVEfmt ', 8);
  out.writeUInt32LE(16, 16); out.writeUInt16LE(1, 20); out.writeUInt16LE(2, 22); out.writeUInt32LE(rate, 24);
  out.writeUInt32LE(rate * 4, 28); out.writeUInt16LE(4, 32); out.writeUInt16LE(16, 34); out.write('data', 36); out.writeUInt32LE(s.length * 4, 40);
  s.forEach((v, i) => { const x = Math.round(v * 32767); out.writeInt16LE(x, 44 + i * 4); out.writeInt16LE(x, 46 + i * 4); });
  return out;
}
const wavPath = S + 'hello.wav';
fs.writeFileSync(wavPath, casToWav(cas, 22050));
await type('NEW\r'); await page.waitForTimeout(500);
t0 = Date.now();
await pick('#cmdRun', wavPath);
console.log('wav run:', last(await waitFor(/TAPE OK/, 120000)), (Date.now() - t0) + 'ms');
console.log('tape after', JSON.stringify(await page.evaluate(() => [tapeInfo(), settings.tape])));

// 6. Survives a reload
await page.reload(); await page.waitForTimeout(1500);
console.log('after reload', JSON.stringify(await page.evaluate(() => [settings.tape && settings.tape.name, tapeInfo()])));
await page.evaluate(() => $('btnMenu').click()); await page.waitForTimeout(300);
await page.evaluate(() => document.querySelector('#tapeName').scrollIntoView());
await page.screenshot({ path: 'test/tape-menu.png' });
console.log(logs.join('\n') || 'no errors'); await b.close();
