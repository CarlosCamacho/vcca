import { chromium } from 'playwright';
import { ROMS as U, TMP as S } from './paths.mjs';
const b = await chromium.launch();
const page = await (await b.newContext({ viewport: { width: 412, height: 915 }, deviceScaleFactor: 2, isMobile: true, hasTouch: true })).newPage();
const logs = []; page.on('pageerror', e => logs.push(e.message)); page.on('dialog', d => d.accept());
await page.goto('http://127.0.0.1:8765/index.html'); await page.waitForTimeout(800);
async function pick(sel, file) { const [fc] = await Promise.all([page.waitForEvent('filechooser'), page.evaluate((s) => document.querySelector(s).click(), sel)]); await fc.setFiles(file); await page.waitForTimeout(600); }
await pick('#wDisk', U + 'disk11.rom'); await pick('#wRom', U + 'coco3.rom');
await page.waitForTimeout(2000);
await pick('#tapeInsert', S + 'hello.cas');
await page.evaluate(() => { tapeControl(1); typeText('CLOAD\r'); });
await page.waitForTimeout(400);
await page.evaluate(() => $('btnMenu').click()); await page.waitForTimeout(700);
await page.evaluate(() => document.querySelector('#tapeName').scrollIntoView());
await page.screenshot({ path: 'tape-menu.png' });
await page.evaluate(() => { $('menu').classList.remove('open'); setPanel('cmd'); }); await page.waitForTimeout(1500);
await page.screenshot({ path: 'tape-cmds.png' });
console.log(await page.evaluate(() => $('status').textContent));
console.log(logs.join('\n') || 'no errors'); await b.close();
