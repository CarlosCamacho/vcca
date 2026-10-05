// Makes the README screenshots: node test/shots.mjs (with web/ served on :8765
// and coco3.rom, disk11.rom and a game disk in roms/).
import { chromium } from 'playwright';
import { ROMS as U, TMP as S } from './paths.mjs';
const OUT = new URL('../docs/screenshots/', import.meta.url).pathname;
const GAME = process.env.GAME || 'balloon.dsk';
const b = await chromium.launch();
async function session(w, h) {
  const page = await (await b.newContext({ viewport: { width: w, height: h }, deviceScaleFactor: 2, isMobile: true, hasTouch: true })).newPage();
  page.on('dialog', d => d.accept());
  await page.goto('http://127.0.0.1:8765/index.html'); await page.waitForTimeout(800);
  page.pick = async (sel, file) => { const [fc] = await Promise.all([page.waitForEvent('filechooser'), page.evaluate((s) => document.querySelector(s).click(), sel)]); await fc.setFiles(file); await page.waitForTimeout(600); };
  await page.pick('#wDisk', U + 'disk11.rom'); await page.pick('#wRom', U + 'coco3.rom');
  await page.waitForTimeout(2500);
  await page.evaluate(() => { const t = document.getElementById('toast'); t.style.visibility = 'hidden'; });
  return page;
}
const type = (page, t) => page.evaluate((t) => typeText(t), t);

// 1. Portrait, keyboard, a BASIC program
let p = await session(412, 915);
await type(p, '10 FOR I=1 TO 3\r20 PRINT "HELLO FROM VCCA";I\r30 NEXT\rRUN\r'); await p.waitForTimeout(3000);
await p.evaluate(() => setPanel('kbd')); await p.waitForTimeout(300);
await p.screenshot({ path: OUT + 'portrait-keyboard.png' });
// 2. Commands tab
await p.evaluate(() => { settings.cmdSlots[10] = 'LOADM"GAME":EXEC|'; buildCommands(); setPanel('cmd'); }); await p.waitForTimeout(300);
await p.screenshot({ path: OUT + 'commands.png' });
// 3. Menu: tape deck with a tape playing
await p.pick('#tapeInsert', S + 'hello.cas');
await p.evaluate(() => { tapeControl(1); typeText('CLOAD\r'); openMenu('tape'); }); await p.waitForTimeout(700);
await p.evaluate(() => document.querySelector('details[data-sec=tape]').scrollIntoView());
await p.screenshot({ path: OUT + 'menu-tape.png' });
// 4. Help
await p.evaluate(() => { $('menu').classList.remove('open'); openHelp(); document.querySelectorAll('#helpDlg details')[8].open = true; }); await p.waitForTimeout(500);
await p.evaluate(() => document.querySelectorAll('#helpDlg details')[8].scrollIntoView());
await p.screenshot({ path: OUT + 'help.png' });
// 5. Landscape, joystick, a game
p = await session(915, 412);
await p.pick('#drives .drive:nth-child(1) button', U + GAME);
await p.evaluate(() => { settings.scan = 1; applyScanlines(); setPanel('joy'); $('menu').classList.remove('open'); });
await type(p, (process.env.GAMECMD || 'LOADM"BALLOON":EXEC') + '\r');
await p.waitForTimeout(+(process.env.GAMEWAIT || 15000));
await p.screenshot({ path: OUT + 'landscape-joystick.png' });
await b.close();
