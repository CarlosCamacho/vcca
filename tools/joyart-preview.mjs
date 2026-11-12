// Renders the on-screen joystick art (src/web/joyart.js) to PNG, as XRoar for
// Android's tools/artpreview does for JoystickArt.java: each controller at
// rest, and pushed up-left with its button held. Compare the two apps' art:
// node tools/joyart-preview.mjs OUT_DIR
import { chromium } from 'playwright';
import fs from 'fs';
import path from 'path';
const out = process.argv[2] || 'build/joyart';
fs.mkdirSync(out, { recursive: true });
const src = fs.readFileSync(new URL('../src/web/joyart.js', import.meta.url), 'utf8');
const b = await chromium.launch();
const page = await b.newPage();
await page.setContent('<body style="margin:0;background:#000"><canvas id="c"></canvas></body>');
await page.addScriptTag({ content: src });
for (const t of ['std', 'deluxe', 'wico']) {
	const png = await page.evaluate((t) => {
		const jw = 1080, jh = 540, cv = document.getElementById('c');
		cv.width = jw * 2; cv.height = jh;
		const ctx = cv.getContext('2d');
		ctx.fillStyle = '#000'; ctx.fillRect(0, 0, cv.width, jh);
		for (let half = 0; half < 2; half++) {
			ctx.save(); ctx.translate(half * jw, 0);
			const g = JoyArt.layout(t, jw, jh);
			JoyArt.drawStatic(ctx, t, g);
			if (half === 0) JoyArt.drawDynamic(ctx, t, g, 0, 0, false, false);
			else JoyArt.drawDynamic(ctx, t, g, -0.75, -0.65, true, false);
			ctx.restore();
		}
		return cv.toDataURL('image/png');
	}, t);
	const f = path.join(out, `joy-${t}.png`);
	fs.writeFileSync(f, Buffer.from(png.split(',')[1], 'base64'));
	console.log('wrote', f);
}
await b.close();
