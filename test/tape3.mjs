import { chromium } from 'playwright';
import fs from 'fs';
import { ROMS as U, TMP as S } from './paths.mjs';
const b = await chromium.launch();
const page = await (await b.newContext()).newPage(); const logs = []; page.on('pageerror', e => logs.push(e.message));
page.on('dialog', d => d.accept());
await page.goto('http://127.0.0.1:8765/index.html'); await page.waitForTimeout(800);
async function pick(sel, file) { const [fc] = await Promise.all([page.waitForEvent('filechooser'), page.evaluate((s) => document.querySelector(s).click(), sel)]); await fc.setFiles(file); await page.waitForTimeout(600); }
await pick('#wDisk', U + 'disk11.rom');
await pick('#wRom', U + 'coco3.rom');
await page.waitForTimeout(3000);
const screen = () => page.evaluate(() => cstr(vcc.vcc_copy_screen_text()));
const type = (t) => page.evaluate((t) => typeText(t), t);
async function waitFor(re, ms = 60000) { const t0 = Date.now(); while (Date.now() - t0 < ms) { const s = await screen(); if (re.test(s)) return s; await page.waitForTimeout(250); } return 'TIMEOUT\n' + await screen(); }
const last = (s) => s.trim().split('\n').slice(-4).join(' / ');
// blank WAV-kind tape, record a program
await page.evaluate(() => { putFile('tape', new Uint8Array(0)); settings.tape = { key: 'tape:x', name: 'rec.wav', kind: TAPE_WAV }; vcc.vcc_tape_insert(TAPE_WAV); });
await type('10 PRINT "WAV OK"\r'); await page.waitForTimeout(500);
await page.evaluate(() => tapeControl(2));
await type('CSAVE"WAVY"\r'); await page.waitForTimeout(1500);
console.log('csave:', last(await waitFor(/CSAVE"WAVY"\s*\n\s*OK/)));
await page.evaluate(() => tapeControl(0));
const raw = await page.evaluate(() => Array.from(Files.get('tape').data));
console.log('recorded', raw.length, 'samples =', (raw.length / 44100).toFixed(2), 's; header', JSON.stringify(await page.evaluate(() => tapeFirstFile(TAPE_WAV, Files.get('tape').data))));
// re-encode as 22050 Hz 16-bit stereo, like a typical capture
const n = Math.floor(raw.length / 2), out = Buffer.alloc(44 + n * 4);
out.write('RIFF', 0); out.writeUInt32LE(36 + n * 4, 4); out.write('WAVEfmt ', 8); out.writeUInt32LE(16, 16); out.writeUInt16LE(1, 20); out.writeUInt16LE(2, 22);
out.writeUInt32LE(22050, 24); out.writeUInt32LE(88200, 28); out.writeUInt16LE(4, 32); out.writeUInt16LE(16, 34); out.write('data', 36); out.writeUInt32LE(n * 4, 40);
for (let i = 0; i < n; i++) { const v = ((raw[2 * i] + raw[2 * i + 1]) / 2 - 128) * 200; out.writeInt16LE(v, 44 + i * 4); out.writeInt16LE(v, 46 + i * 4); }
fs.writeFileSync(S + 'wavy22k.wav', out);
await type('NEW\r'); await page.waitForTimeout(500);
for (const f of [+(process.argv[2] || 1)]) {
  await page.evaluate((f) => { settings.tapeFast = f; applyConfig(); }, f);
  const t0 = Date.now();
  await pick('#cmdRun', S + 'wavy22k.wav');
  console.log(f ? 'fast' : 'real time', 'Run of 22 kHz stereo WAV:', last(await waitFor(/WAV OK/, 90000)), (Date.now() - t0) + 'ms');
}
console.log(logs.join('\n') || 'no errors'); await b.close();
