import { chromium } from 'playwright';
import { ROMS as U, TMP as S } from './paths.mjs';
const b = await chromium.launch();
const page = await (await b.newContext({ viewport: { width: 412, height: 915 }, deviceScaleFactor: 2.6, isMobile: true, hasTouch: true })).newPage();
const logs = []; page.on('pageerror', e => logs.push(e.message)); page.on('dialog', d => d.accept());
await page.goto('http://127.0.0.1:8765/index.html'); await page.waitForTimeout(800);
async function pick(sel, file) { const [fc] = await Promise.all([page.waitForEvent('filechooser'), page.evaluate((s) => document.querySelector(s).click(), sel)]); await fc.setFiles(file); await page.waitForTimeout(600); }
await pick('#wDisk', U + 'disk11.rom'); await pick('#wRom', U + 'coco3.rom'); await page.waitForTimeout(2500);
const screen = () => page.evaluate(() => cstr(vcc.vcc_copy_screen_text()));
const type = (t) => page.evaluate((t) => typeText(t), t);
const last = async (n = 3) => (await screen()).trim().split('\n').slice(-n).join(' / ');
// 1. printer
await page.evaluate(() => printerStart());
await type('PRINT#-2,"HELLO PRINTER"\r'); await page.waitForTimeout(4000);
await type('10 REM LISTED\r'); await page.waitForTimeout(800);
await type('LLIST\r'); await page.waitForTimeout(4000);
console.log('printer:', JSON.stringify(await page.evaluate(() => printerText())));
// 2. joystick ports: on-screen stick on right; JOYSTK(0)=right X. Then move stick to left port.
await page.evaluate(() => { joy.x = 63; joy.y = 0; sendJoy(); });
await type('CLS:PRINT JOYSTK(0);JOYSTK(1);JOYSTK(2);JOYSTK(3)\r'); await page.waitForTimeout(1500);
console.log('stick on right port (RX RY LX LY):', await last(2));
await page.evaluate(() => { settings.joyPorts.right.src = 'none'; settings.joyPorts.left.src = 'keys'; });
await page.keyboard.down('ArrowRight'); await page.keyboard.down('ArrowDown'); await page.waitForTimeout(200);
await type('CLS:PRINT JOYSTK(0);JOYSTK(1);JOYSTK(2);JOYSTK(3)\r'); await page.waitForTimeout(1500);
await page.keyboard.up('ArrowRight'); await page.keyboard.up('ArrowDown');
console.log('keyboard on left port, right+down held:', await last(2));
// 3. F-keys
await page.keyboard.press('F4'); await page.waitForTimeout(200);
console.log('F4 → overclock', await page.evaluate(() => settings.overclock), '; F6 →', await page.keyboard.press('F6').then(() => page.evaluate(() => settings.rgb)));
await page.keyboard.press('F6'); await page.keyboard.press('F3');
// 4. menu screenshots
await page.evaluate(() => { settings.joyPorts.right.src = 'stick'; settings.joyPorts.left.src = 'none'; openMenu(); }); await page.waitForTimeout(400);
await page.evaluate(() => { for (const d of document.querySelectorAll('#menuPanel details')) d.open = false; });
await page.screenshot({ path: 'm-closed.png' });
for (const sec of ['joystick', 'tape', 'printer', 'about']) {
  await page.evaluate((s) => document.querySelector(`details[data-sec=${s}] summary`).click(), sec); await page.waitForTimeout(500);
  await page.screenshot({ path: `m-${sec}.png` });
}
console.log(logs.join('\n') || 'no errors'); await b.close();
