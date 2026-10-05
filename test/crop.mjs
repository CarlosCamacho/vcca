import { chromium } from 'playwright';
import fs from 'fs';
const fr = process.argv.slice(3); const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 460, height: 40 * fr.length } });
const html = fr.map(f => `<div style="width:460px;height:40px;background:url(data:image/png;base64,${fs.readFileSync(`c-${f}.png`).toString('base64')}) -100px -100px"></div>`).join('');
await p.setContent(`<body style="margin:0">${html}</body>`); await p.screenshot({ path: process.argv[2] }); await b.close();
