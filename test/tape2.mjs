import { chromium } from 'playwright';
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
const fast = +(process.argv[3] || 1);
await page.evaluate((f) => { settings.tapeFast = f; applyConfig(); }, fast);
await pick('#tapeInsert', S + process.argv[2]);
await page.evaluate(() => tapeControl(1));
await type('CLOAD\r');
for (let i = 0; i < 80; i++) { await page.waitForTimeout(100); const s = await screen(); const inf = await page.evaluate(() => tapeInfo()); console.log(i, JSON.stringify(inf), s.trim().split('\n').slice(-2).join(' / ')); if (/OK\s*$/.test(s.trim()) && i > 30) break; }
await type('LIST\r'); await page.waitForTimeout(1500);
console.log((await screen()).trim().split('\n').slice(-5).join(' / '));
console.log(logs.join('\n') || 'no errors'); await b.close();
