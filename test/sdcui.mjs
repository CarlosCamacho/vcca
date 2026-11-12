// The CoCoSDC in the real front end (Chromium): the SD card's files go in
// through the app, the CoCo program in test/sdcrom.raw (the machine's ROM
// here, so no Tandy ROM is needed) uses them, and the results are checked on
// both sides: what the CoCo read, what reached IndexedDB, the SD card manager,
// Run… with a .dsk, and the card surviving a reload.
// node test/sdcui.mjs   (front end served on :8765: test/run.sh does it)
import { chromium } from 'playwright';
import fs from 'fs';
import { TMP } from './paths.mjs';
const here = new URL('.', import.meta.url).pathname;
let fails = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) fails++; };

const b = await chromium.launch();
const ctx = await b.newContext();
const page = await ctx.newPage();
const logs = []; page.on('pageerror', (e) => logs.push(e.message)); page.on('dialog', (d) => d.accept());
await page.goto('http://127.0.0.1:8765/index.html'); await page.waitForTimeout(800);
const [fc] = await Promise.all([page.waitForEvent('filechooser'), page.evaluate(() => document.querySelector('#wRom').click())]);
await fc.setFiles(here + 'sdcrom.raw');
await page.waitForTimeout(1000);

// The SD card and the cartridge, as a user would set them up.
await page.evaluate(async () => {
  const disk = (fn) => { const d = new Uint8Array(630 * 256); for (let n = 0; n < 630; n++) for (let i = 0; i < 256; i++) d[n * 256 + i] = fn(n, i) & 255; return d; };
  SD.add('STARTUP.CFG', new TextEncoder().encode('0=TEST.DSK\r\n'));
  SD.add('TEST.DSK', disk((n, i) => n * 7 + i));
  SD.add('GAMES/TWO.DSK', disk((n, i) => n * 3 + i + 0x40));
  SD.add('GAMES/TWO.BAK', disk(() => 0x11));
  SD.add('RO.DSK', disk(() => 0xEE));
  settings.slots[3] = { type: 13 };
  saveSettings();
  await hardReset();
});
for (let i = 0; i < 40 && !(await page.evaluate(() => vcc.vcc_peek(0x10ff) === 0xa5)); i++) await page.waitForTimeout(100);
const peek = (a) => page.evaluate((a) => vcc.vcc_peek(a), a);
check(await peek(0x10ff) === 0xa5, 'the CoCo program ran to the end');
check(await peek(0x1000) === 0, 'it found the CoCoSDC in slot 4');
check(await peek(0x1025) === 0, 'it read LSN 5 from TEST.DSK, mounted by STARTUP.CFG');
const lsn5 = await page.evaluate(() => Array.from({ length: 256 }, (_, i) => vcc.vcc_peek(0x1100 + i)));
check(lsn5.every((v, i) => v === ((5 * 7 + i) & 255)), 'LSN 5 is right, byte for byte');
check(await peek(0x1022) === 0, 'M:GAMES/TWO mounted the image in the folder');

// Persistence: the CoCo's write and its new image reach IndexedDB.
const stored = await page.evaluate(async () => {
  await saveDirtyDisks();
  const t = new Uint8Array(await Store.get('sdf:TEST.DSK'));
  const n = await Store.get('sdf:NEW.DSK');
  return { lsn7: Array.from(t.subarray(7 * 256, 8 * 256)), newLen: n ? n.byteLength : 0, files: settings.sdFiles };
});
check(stored.lsn7.every((v, i) => v === 255 - i), 'the sector the CoCo wrote is in IndexedDB');
check(stored.newLen === 161280 && stored.files.includes('NEW.DSK'), `N:NEW.DSK is stored and listed (${stored.newLen} bytes)`);

// The SD card manager.
await page.evaluate(() => { $('btnMenu').click(); document.querySelector('[data-sec=sdcard]').open = true; }); await page.waitForTimeout(300);
const listing = await page.evaluate(() => $('sdList').innerText);
check(['GAMES/', 'NEW.DSK', 'RO.DSK', 'STARTUP.CFG', 'TEST.DSK'].every((n) => listing.includes(n)), 'the manager lists the root');
const mounts = await page.evaluate(() => $('sdMounts').innerText);
check(/TEST\.DSK/.test(mounts) && /NEW\.DSK/.test(mounts), `it shows what is mounted (${mounts.replace(/\s+/g, ' ').trim()})`);
await page.evaluate(() => { const r = [...$('sdList').children].find((x) => x.innerText.startsWith('RO.DSK')); [...r.querySelectorAll('button')].find((b) => b.textContent === '1').click(); });
await page.waitForTimeout(200);
check(await page.evaluate(() => cstr(vcc.vcc_sdc_mounted(1))) === 'RO.DSK', 'tapping 1 on RO.DSK mounts it in drive 1');
await page.evaluate(() => { [...$('sdList').children].find((x) => x.innerText.startsWith('GAMES/')).querySelector('button').click(); });
await page.waitForTimeout(200);
const inGames = await page.evaluate(() => $('sdPath').innerText + ' ' + $('sdList').innerText);
check(inGames.includes('/GAMES') && inGames.includes('TWO.DSK') && inGames.includes('TWO.BAK'), 'opening GAMES/ lists its files');
await page.evaluate(() => $('menu').classList.remove('open'));

// Run… with a .dsk goes onto the SD card and into drive 0.
const game = new Uint8Array(161280).fill(0x5A);
fs.writeFileSync(TMP + 'Long Game Name.dsk', game);
const [fc2] = await Promise.all([page.waitForEvent('filechooser'), page.evaluate(() => $('btnRun').click())]);
await fc2.setFiles(TMP + 'Long Game Name.dsk');
await page.waitForTimeout(600);
const run = await page.evaluate(() => ({ m: cstr(vcc.vcc_sdc_mounted(0)), files: SD.files() }));
check(run.m === 'LONGGAME.DSK' && run.files.includes('LONGGAME.DSK'), `Run… put the disk on the card as an 8.3 name and mounted it (${run.m})`);

// A reload brings the card back.
await page.reload(); await page.waitForTimeout(1500);
const after = await page.evaluate(() => ({ files: SD.files(), dirs: [...SD.dirs], t7: Array.from(Files.get('sd:TEST.DSK')?.data.subarray(7 * 256, 7 * 256 + 4) || []) }));
check(['GAMES/TWO.BAK', 'GAMES/TWO.DSK', 'LONGGAME.DSK', 'NEW.DSK', 'RO.DSK', 'STARTUP.CFG', 'TEST.DSK'].every((f) => after.files.includes(f)) && after.dirs.includes('GAMES'), `after a reload the card holds the same files (${after.files.join(' ')})`);
check(after.t7.join(',') === '255,254,253,252', 'and the CoCo\'s write');

await b.close();
if (logs.length) { console.log(logs.join('\n')); fails++; }
console.log(fails ? `FAIL: ${fails} checks failed` : 'no errors');
process.exit(fails ? 1 : 0);
