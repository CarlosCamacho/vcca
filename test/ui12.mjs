import { chromium } from 'playwright';
import { ROMS as U, TMP as S } from './paths.mjs';
import { check, done } from './check.mjs';
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
const printed = await page.evaluate(() => printerText());
check(printed === 'HELLO PRINTER\n10 REM LISTED\n', `printer captured PRINT#-2 and LLIST (${JSON.stringify(printed)})`);
// 2. joystick ports: on-screen stick on right; JOYSTK(0)=right X. Then move stick to left port.
await page.evaluate(() => { joy.x = 63; joy.y = 0; sendJoy(); });
await type('CLS:PRINT JOYSTK(0);JOYSTK(1);JOYSTK(2);JOYSTK(3)\r'); await page.waitForTimeout(1500);
const nums = (s) => (s.match(/-?\d+/g) || []).map(Number);
let j = nums((await last(2)).split(' / ')[0]);
check(j.length === 4 && j[0] >= 60 && j[1] <= 2 && j[2] >= 28 && j[2] <= 34 && j[3] >= 28 && j[3] <= 34, `on-screen stick pushed right-up on the right port, left port centred (RX RY LX LY = ${j.join(' ')})`);
await page.evaluate(() => { settings.joyPorts.right.src = 'none'; settings.joyPorts.left.src = 'keys'; });
await page.keyboard.down('ArrowRight'); await page.keyboard.down('ArrowDown'); await page.waitForTimeout(200);
await type('CLS:PRINT JOYSTK(0);JOYSTK(1);JOYSTK(2);JOYSTK(3)\r'); await page.waitForTimeout(1500);
await page.keyboard.up('ArrowRight'); await page.keyboard.up('ArrowDown');
j = nums((await last(2)).split(' / ')[0]);
check(j.length === 4 && j[2] >= 60 && j[3] >= 60, `arrow keys on the left port, right+down held (RX RY LX LY = ${j.join(' ')})`);
// 3. F-keys
await page.keyboard.press('F4'); await page.waitForTimeout(200);
const oc = await page.evaluate(() => settings.overclock);
check(oc === 4, `F4 raises the overclock to 4 (${oc})`);
const rgb0 = await page.evaluate(() => +settings.rgb);
await page.keyboard.press('F6');
const rgb1 = await page.evaluate(() => +settings.rgb);
check(rgb1 === 1 - rgb0, `F6 switches the monitor type (${rgb0} → ${rgb1})`);
await page.keyboard.press('F6'); await page.keyboard.press('F3');
// 4. menu screenshots
await page.evaluate(() => { settings.joyPorts.right.src = 'stick'; settings.joyPorts.left.src = 'none'; openMenu(); }); await page.waitForTimeout(400);
await page.evaluate(() => { for (const d of document.querySelectorAll('#menuPanel details')) d.open = false; });
await page.screenshot({ path: S + 'm-closed.png' });
for (const sec of ['joystick', 'tape', 'printer', 'about']) {
  await page.evaluate((s) => document.querySelector(`details[data-sec=${s}] summary`).click(), sec); await page.waitForTimeout(500);
  await page.screenshot({ path: S + `m-${sec}.png` });
}
await done(logs, b);
