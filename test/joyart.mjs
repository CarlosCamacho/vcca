// The on-screen joystick drawn as a real controller (src/web/joyart.js): for
// each look, a drag on the stick, a press of the drawn buttons, and whether the
// stick springs back. Needs no ROM: it checks what the front end would send to
// the CoCo (the joy state behind vcc_joystick). Screenshots go to the temp folder.
// node test/joyart.mjs   (front end served on :8765: test/run.sh does it)
import { chromium } from 'playwright';
import { TMP as S } from './paths.mjs';
import { check, done } from './check.mjs';

const b = await chromium.launch();
const page = await (await b.newContext({ viewport: { width: 412, height: 915 }, deviceScaleFactor: 2 })).newPage();
const logs = []; page.on('pageerror', (e) => logs.push(e.message)); page.on('dialog', (d) => d.accept());
await page.goto('http://127.0.0.1:8765/index.html'); await page.waitForTimeout(800);
await page.evaluate(() => { document.querySelectorAll('.dialog.open,#welcome').forEach((d) => d.classList.remove('open')); setPanel('joy'); });
await page.waitForTimeout(300);

const J = () => page.evaluate(() => ({ x: joy.x, y: joy.y, b: joy.b, look: settings.joyLook, center: +settings.joyCenter, art: document.body.classList.contains('joy-art') }));
// Page coordinates of a point in the art's own layout.
const at = (fn) => page.evaluate((fn) => { const r = $('stickArt').getBoundingClientRect(); const g = art.g; const [x, y] = new Function('g', 'JoyArt', 'art', `return (${fn})(g, JoyArt, art)`)(g, JoyArt, art); return [r.left + x, r.top + y]; }, fn.toString());
const look = async (v) => { await page.evaluate((v) => { $('selJoyLook').value = v; $('selJoyLook').onchange(); }, v); await page.waitForTimeout(200); };

check((await J()).look === 'deluxe' && (await J()).art, 'the TRS-80 Deluxe Joystick is the default look');
const inked = await page.evaluate(() => { const c = $('stickArt'), d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let n = 0; for (let i = 3; i < d.length; i += 4) if (d[i]) n++; return n / (d.length / 4); });
check(inked > 0.5, `the controller is drawn (${Math.round(inked * 100)}% of the canvas)`);

for (const type of ['deluxe', 'std', 'wico']) {
  await look(type);
  const s0 = await J();
  check(s0.center === (type === 'std' ? 0 : 1), `${type}: springs back ${type === 'std' ? 'off' : 'on'}, as the real controller`);
  // drag the stick, from a touch on it just beside the Wico's tip (inside the tip zone
  // XRoar uses, outside VCCA's), to the upper left past its travel
  const [cx, cy] = await at((g) => [g.sx + g.R * 0.37, g.sy]);
  const [ux, uy] = await at((g) => [g.sx - g.R * 2, g.sy - g.R * 2]);
  await page.mouse.move(cx, cy); await page.mouse.down();
  await page.mouse.move(ux, uy, { steps: 5 });
  const held = await J();
  check(held.x >= 0 && held.y >= 0 && held.x <= 12 && held.y <= 12, `${type}: the stick pushed up-left past its travel reads near 0,0, never below (${held.x},${held.y})`);
  await page.screenshot({ path: S + `joyart-${type}-held.png` });
  await page.mouse.up();
  const rel = await J();
  if (type === 'std') check(rel.x <= 12 && rel.y <= 12, `std: let go, it stays where it was (${rel.x},${rel.y})`);
  else check(Math.abs(rel.x - 32) <= 1 && Math.abs(rel.y - 32) <= 1, `${type}: let go, it springs back to the middle (${rel.x},${rel.y})`);
  if (type === 'std') { await look('std'); }   // re-centre for the button test
  // the case's red button is button 1
  const [fx, fy] = await at((g) => [g.f1x, g.f1y]);
  await page.mouse.move(fx, fy); await page.mouse.down();
  const pressed = await J();
  check((pressed.b & 1) === 1 && Math.abs(pressed.x - 32) <= 1, `${type}: the red button on the case is button 1, and does not move the stick (b=${pressed.b})`);
  await page.screenshot({ path: S + `joyart-${type}-fire.png` });
  await page.mouse.up();
  check(((await J()).b & 1) === 0, `${type}: released, button 1 is up`);
  if (type === 'wico') {
    const [tx, ty] = await at((g, JoyArt, art) => [JoyArt.tipX(g, art.x), JoyArt.tipY(g, art.y)]);
    await page.mouse.move(tx, ty); await page.mouse.down();
    const tip = await J();
    check((tip.b & 2) === 2 && Math.abs(tip.x - 32) <= 1, `wico: the white tip is button 2, and does not move the stick (b=${tip.b})`);
    await page.mouse.up();
    check(((await J()).b & 2) === 0, 'wico: released, button 2 is up');
  }
  // a touch on the case away from the stick and buttons does nothing
  const [ex, ey] = await at((g) => [g.cx - g.S * 0.44, g.cy + g.S * 0.44]);
  await page.mouse.move(ex, ey); await page.mouse.down();
  const empty = await J();
  check(empty.b === 0 && Math.abs(empty.x - 32) <= 1 && Math.abs(empty.y - 32) <= 1, `${type}: a touch on the bare case does nothing (${empty.x},${empty.y},b=${empty.b})`);
  await page.mouse.up();
}

// the FIRE button beside the controller still works
await look('deluxe');
const f = await page.evaluate(() => { const r = $('fire1').getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; });
await page.mouse.move(...f); await page.mouse.down();
check(((await J()).b & 1) === 1, 'the separate FIRE button still presses button 1');
await page.mouse.up();

// Simple: the old stick
await look('simple');
check(!(await J()).art, 'Simple brings back the plain stick');
const sk = await page.evaluate(() => { const r = $('stick').getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2, r.width]; });
await page.mouse.move(sk[0], sk[1]); await page.mouse.down(); await page.mouse.move(sk[0] + sk[2], sk[1], { steps: 4 });
check((await J()).x >= 60, `the plain stick still drives the port (${(await J()).x})`);
await page.mouse.up();

// landscape screenshots of each look
await page.setViewportSize({ width: 915, height: 412 });
for (const t of ['deluxe', 'std', 'wico']) { await look(t); await page.waitForTimeout(300); await page.screenshot({ path: S + `joyart-${t}-land.png` }); }
check(await page.evaluate(() => $('stickArt').clientWidth > 100), 'in landscape the controller is still drawn full size');

await done(logs, b);
