import { chromium } from 'playwright';
import { ROMS as U, TMP as S } from './paths.mjs';
const b = await chromium.launch();
const page = await (await b.newContext()).newPage(); const logs = []; page.on('pageerror', e => logs.push(e.message)); page.on('dialog', d => d.accept());
await page.goto('http://127.0.0.1:8765/index.html'); await page.waitForTimeout(800);
async function pick(sel, file) { const [fc] = await Promise.all([page.waitForEvent('filechooser'), page.evaluate((s) => document.querySelector(s).click(), sel)]); await fc.setFiles(file); await page.waitForTimeout(600); }
await pick('#wDisk', U + 'disk11.rom'); await pick('#wRom', U + 'coco3.rom'); await page.waitForTimeout(2500);
const screen = () => page.evaluate(() => cstr(vcc.vcc_copy_screen_text()));
const type = (t) => page.evaluate((t) => typeText(t), t);
async function waitFor(re, ms = 60000) { const t0 = Date.now(); while (Date.now() - t0 < ms) { const s = await screen(); if (re.test(s)) return s; await page.waitForTimeout(250); } return 'TIMEOUT\n' + await screen(); }
const last = (s) => s.trim().split('\n').slice(-3).join(' / ');
for (const kind of [1, 2]) {
  await page.evaluate(async (k) => { await removeTape(); putFile('tape', new Uint8Array(0)); settings.tape = { key: 'tape:x', name: 'two', kind: k }; vcc.vcc_tape_insert(k); }, kind);
  for (const [n, msg] of [['ONE', 'FIRST'], ['TWO', 'SECOND']]) {
    await type(`NEW\r10 PRINT "${msg}"\r`); await page.waitForTimeout(800);
    await page.evaluate(() => tapeControl(2));
    await type(`CSAVE"${n}"\r`); await page.waitForTimeout(1500);
    await waitFor(new RegExp(`CSAVE"${n}"\\s*\\n\\s*OK`));
    await page.evaluate(() => tapeControl(0));
  }
  const fwd = await page.evaluate(() => { tapeRewind(); tapeForward(); return [tapeInfo().pos, tapeInfo().size]; });
  await type('NEW\r'); await page.waitForTimeout(500);
  await page.evaluate(() => tapeControl(1));
  await type('CLOAD\r'); await page.waitForTimeout(1000);
  await waitFor(/CLOAD[\s\S]*OK/);
  await type('RUN\r');
  console.log(kind === 1 ? 'WAV' : 'CAS', 'FWD to', fwd.join(' of '), '→', last(await waitFor(/SECOND|FIRST/)));
}
console.log(logs.join('\n') || 'no errors'); await b.close();
