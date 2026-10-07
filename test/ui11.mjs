import { chromium } from 'playwright';
import { ROMS as U, TMP as S } from './paths.mjs';
const [w, h, dpr, tag] = process.argv.slice(2);
const b = await chromium.launch();
const page = await (await b.newContext({ viewport: { width: +w, height: +h }, deviceScaleFactor: +dpr, isMobile: true, hasTouch: true })).newPage();
const logs = []; page.on('pageerror', e => logs.push(e.message)); page.on('dialog', d => d.accept());
await page.goto('http://127.0.0.1:8765/index.html'); await page.waitForTimeout(800);
async function pick(sel, file) { const [fc] = await Promise.all([page.waitForEvent('filechooser'), page.evaluate((s) => document.querySelector(s).click(), sel)]); await fc.setFiles(file); await page.waitForTimeout(600); }
await pick('#wDisk', U + 'disk11.rom'); await pick('#wRom', U + 'coco3.rom');
await page.waitForTimeout(2500);
const rect = (id) => page.evaluate((id) => { const r = document.getElementById(id).getBoundingClientRect(); return [r.x, r.y, r.width, r.height].map(Math.round); }, id);
await page.evaluate((sc) => { settings.scan = sc; applyScanlines(); setPanel('joy'); }, +(process.env.SCAN || 1));
await page.waitForTimeout(500);
console.log(tag, 'top', await rect('top'), 'menu', await rect('btnMenu'), 'help', await rect('btnHelp'), 'screen', await rect('screenWrap'), 'stick', await rect('stick'), 'fires', await rect('fires'), 'scan', await rect('scan'));
await page.screenshot({ path: S + `${tag}-joy.png` });
await page.screenshot({ path: S + `${tag}-crop.png`, clip: { x: 220, y: 100, width: 260, height: 90 } });
if (tag === 'phone') {
  await page.evaluate(() => setPanel('kbd'));
  await page.evaluate(() => $('btnMenu').click()); await page.waitForTimeout(400);
  await page.screenshot({ path: S + `${tag}-menu.png` });
  await page.evaluate(() => document.querySelector('details[data-sec=tape] summary').click()); await page.waitForTimeout(500);
  await pick('#tapeInsert', S + 'hello.cas');
  await page.waitForTimeout(300);
  const fwd = [];
  for (let i = 0; i < 3; i++) { await page.evaluate(() => $('tapeFwd').click()); await page.waitForTimeout(100); fwd.push(await page.evaluate(() => tapeInfo().pos)); }
  console.log('cas FWD positions', fwd.join(' → '));
  await page.evaluate(() => { $('tapeRew').click(); $('tapePlay').click(); typeText('CLOAD\r'); }); await page.waitForTimeout(1200);
  await page.screenshot({ path: S + `${tag}-tape.png` });
  await page.evaluate(() => $('menu').classList.remove('open'));
  await page.evaluate(() => $('btnHelp').click()); await page.waitForTimeout(300);
  await page.evaluate(() => document.querySelectorAll('#helpDlg details')[2].querySelector('summary').click()); await page.waitForTimeout(500);
  await page.screenshot({ path: S + `${tag}-help.png` });
  await page.evaluate(() => $('helpDlg').classList.remove('open'));
  // wav FWD: emulator-recorded WAV with two programs
  await pick('#tapeInsert', S + 'wavy22k.wav');
  console.log('wav FWD from 0 →', await page.evaluate(() => { tapeForward(); return tapeInfo().pos; }), 'of', await page.evaluate(() => tapeInfo().size));
}
console.log(logs.join('\n') || 'no errors'); await b.close();
