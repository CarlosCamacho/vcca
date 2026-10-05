import { chromium } from 'playwright';
const [w, h, out, extra] = process.argv.slice(2);
const b = await chromium.launch();
const page = await (await b.newContext({ viewport: { width: +w, height: +h }, deviceScaleFactor: 1.5, isMobile: true, hasTouch: true })).newPage();
await page.goto('http://127.0.0.1:8765/index.html'); await page.waitForTimeout(1000);
await page.evaluate((x) => { document.getElementById('welcome').classList.remove('open'); setPanel('joy'); if (x) eval(x); }, extra || '');
await page.waitForTimeout(400);
console.log(JSON.stringify(await page.evaluate(() => Object.fromEntries(['top', 'screenWrap', 'stick', 'fires', 'btnMenu'].map(id => { const r = document.getElementById(id).getBoundingClientRect(); return [id, [r.x, r.y, r.width, r.height].map(Math.round)]; })))));
await page.screenshot({ path: out }); await b.close();
