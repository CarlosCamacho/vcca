// The on-screen joystick drawn as a real controller, seen from above and lit
// from the top left. A port to the browser's canvas of JoystickArt.java and
// ArtUtil.java from the owner's XRoar for Android
// (github.com/CarlosCamacho/xroar-android-64), kept close to the original so
// the two apps look the same:
//
//   "std"    Radio Shack joystick (26-3008): concentric rings, a black stick,
//            one red button at the top edge. Not self-centering.
//   "deluxe" TRS-80 Deluxe Joystick (26-3012): pebbled cream case, black
//            pyramid well, red button top left, name plate. Self-centering.
//   "wico"   Wico-style: black case on a red base, domed boss, red base button
//            top left, red ball top with a white button in its tip.
//            Self-centering, two buttons.
//
// drawStatic() is everything that does not move (cached by the caller);
// drawDynamic() is the stick and the buttons, drawn on every change.
'use strict';

const JoyArt = (() => {
  // Android colour ints (0xAARRGGBB) to CSS.
  const css = (c) => `rgba(${(c >>> 16) & 255},${(c >>> 8) & 255},${c & 255},${((c >>> 24) & 255) / 255})`;
  const stops = (g, colors, pos) => {
    colors.forEach((c, i) => g.addColorStop(pos ? pos[i] : (colors.length === 1 ? 0 : i / (colors.length - 1)), css(c)));
    return g;
  };
  function rrect(ctx, l, t, r, b, rad) {
    rad = Math.max(0, Math.min(rad, (r - l) / 2, (b - t) / 2));
    ctx.beginPath();
    ctx.moveTo(l + rad, t);
    ctx.arcTo(r, t, r, b, rad);
    ctx.arcTo(r, b, l, b, rad);
    ctx.arcTo(l, b, l, t, rad);
    ctx.arcTo(l, t, r, t, rad);
    ctx.closePath();
  }
  const R = (l, t, r, b) => ({ l, t, r, b, w: r - l, h: b - t });
  const circle = (ctx, x, y, r) => { ctx.beginPath(); ctx.arc(x, y, Math.max(0, r), 0, Math.PI * 2); };

  // ------------------------------------------------------------- ArtUtil
  let grainTex = null, streakTex = null;
  function rng(seed) { let s = seed >>> 0; return () => { s = (s * 1664525 + 1013904223) >>> 0; return s / 4294967296; }; }
  function grain() {
    if (grainTex) return grainTex;
    const n = 128, cv = document.createElement('canvas'); cv.width = cv.height = n;
    const x = cv.getContext('2d'), img = x.createImageData(n, n), r = rng(6809);
    for (let i = 0; i < n * n; i++) {
      const a = Math.floor(Math.pow(r(), 2.2) * 255), v = r() < 0.5 ? 255 : 0;
      img.data.set([v, v, v, a], i * 4);
    }
    x.putImageData(img, 0, 0);
    return (grainTex = cv);
  }
  function streaks() {
    if (streakTex) return streakTex;
    const w = 256, h = 64, cv = document.createElement('canvas'); cv.width = w; cv.height = h;
    const x = cv.getContext('2d'), img = x.createImageData(w, h), r = rng(1980);
    for (let y = 0; y < h; y++) {
      const base = r();
      for (let i = 0; i < w; i++) {
        const v = base * 0.7 + r() * 0.3, a = Math.floor(Math.abs(v - 0.5) * 2 * 140), c = v > 0.5 ? 255 : 0;
        img.data.set([c, c, c, a], (y * w + i) * 4);
      }
    }
    x.putImageData(img, 0, 0);
    return (streakTex = cv);
  }
  function texture(ctx, tex, r, rad, strength) {
    ctx.save();
    ctx.globalAlpha = strength / 255;
    // Grain at device pixels, as on Android: undo the caller's scale for the pattern.
    const pat = ctx.createPattern(tex, 'repeat'), m = ctx.getTransform ? ctx.getTransform() : null;
    if (m && pat.setTransform && window.DOMMatrix) pat.setTransform(new DOMMatrix([1 / m.a, 0, 0, 1 / m.d, 0, 0]));
    ctx.fillStyle = pat;
    rrect(ctx, r.l, r.t, r.r, r.b, rad);
    ctx.fill();
    ctx.restore();
  }
  function vgrad(ctx, r, rad, colors, pos) {
    ctx.fillStyle = stops(ctx.createLinearGradient(0, r.t, 0, r.b), colors, pos);
    rrect(ctx, r.l, r.t, r.r, r.b, rad);
    ctx.fill();
  }
  function dgrad(ctx, r, rad, from, to) {
    ctx.fillStyle = stops(ctx.createLinearGradient(r.l, r.t, r.r, r.b), [from, to]);
    rrect(ctx, r.l, r.t, r.r, r.b, rad);
    ctx.fill();
  }
  function sphere(ctx, x, y, r, colors, pos) {
    const gx = x - r * 0.35, gy = y - r * 0.4;
    ctx.fillStyle = stops(ctx.createRadialGradient(gx, gy, 0, gx, gy, r * 1.45), colors, pos);
    circle(ctx, x, y, r);
    ctx.fill();
  }
  const alpha = (c, a) => ((c & 0x00FFFFFF) | ((a & 255) << 24)) >>> 0;
  function softShadow(ctx, x, y, r, strength) {
    ctx.fillStyle = stops(ctx.createRadialGradient(x, y, 0, x, y, r), [alpha(0, strength), alpha(0, strength >> 1), 0], [0, 0.55, 1]);
    circle(ctx, x, y, r);
    ctx.fill();
  }
  function specular(ctx, x, y, r, strength) {
    ctx.fillStyle = stops(ctx.createRadialGradient(x, y, 0, x, y, r), [alpha(0xFFFFFFFF, strength), 0x00FFFFFF]);
    circle(ctx, x, y, r);
    ctx.fill();
  }

  // ------------------------------------------------------------- JoystickArt
  function layout(type, w, h) {
    // XRoar's pad is wide; VCCA's is about square, so the case fills it.
    const g = { S: Math.min(h * 0.94, w * 0.94), cx: w / 2, cy: h / 2, hasFire2: false, f2r: 0 };
    const S = g.S;
    if (type === 'deluxe') Object.assign(g, { sx: g.cx + S * 0.10, sy: g.cy - S * 0.06, R: S * 0.23, f1x: g.cx - S * 0.36, f1y: g.cy - S * 0.36, f1r: S * 0.15 });
    else if (type === 'wico') Object.assign(g, { sx: g.cx + S * 0.02, sy: g.cy + S * 0.05, R: S * 0.26, f1x: g.cx - S * 0.32, f1y: g.cy - S * 0.32, f1r: S * 0.15, hasFire2: true, f2r: S * 0.08 });
    else Object.assign(g, { sx: g.cx, sy: g.cy + S * 0.05, R: S * 0.28, f1x: g.cx, f1y: g.cy - S * 0.47, f1r: S * 0.15 });
    return g;
  }
  const travel = (type) => (type === 'wico' ? 0.75 : 0.62);
  const topX = (g, type, x) => g.sx + x * g.R * travel(type);
  const topY = (g, type, y) => g.sy + y * g.R * travel(type);
  const tipX = (g, x) => topX(g, 'wico', x) + x * g.S * 0.02;
  const tipY = (g, y) => topY(g, 'wico', y) + y * g.S * 0.02;
  const box = (g, inset) => { const h = g.S / 2 - inset; return R(g.cx - h, g.cy - h, g.cx + h, g.cy + h); };

  function blurred(ctx, radius, fn) {
    ctx.save();
    ctx.filter = `blur(${radius * 0.5}px)`;
    fn();
    ctx.restore();
  }
  function dropShadow(ctx, r, rad, S) {
    blurred(ctx, S * 0.035, () => {
      ctx.fillStyle = css(0xC0000000);
      rrect(ctx, r.l + S * 0.01, r.t + S * 0.03, r.r + S * 0.025, r.b + S * 0.045, rad);
      ctx.fill();
    });
  }
  function bevel(ctx, r, rad, w, light, dark) {
    ctx.lineWidth = w;
    ctx.strokeStyle = stops(ctx.createLinearGradient(r.l, r.t, r.r, r.b), [light, 0x00FFFFFF, 0x00000000, dark], [0, 0.45, 0.55, 1]);
    rrect(ctx, r.l, r.t, r.r, r.b, rad);
    ctx.stroke();
  }
  function ring(ctx, x, y, r, w, color) {
    ctx.lineWidth = w;
    ctx.strokeStyle = css(color);
    circle(ctx, x, y, r);
    ctx.stroke();
  }
  function text(ctx, s, x, y, size, color, italic) {
    ctx.font = `${italic ? 'italic ' : ''}bold ${size}px sans-serif`;
    ctx.textAlign = 'center';
    ctx.fillStyle = css(color);
    ctx.fillText(s, x, y);
  }
  function emboss(ctx, s, x, y, size, color, italic) {
    const d = Math.max(1, size * 0.05);
    text(ctx, s, x + d, y + d, size, 0x90000000, italic);
    text(ctx, s, x - d * 0.6, y - d * 0.6, size, 0x28FFFFFF, italic);
    text(ctx, s, x, y, size, color, italic);
  }
  function line(ctx, x0, y0, x1, y1, w, color, cap) {
    ctx.lineWidth = w;
    ctx.lineCap = cap || 'butt';
    ctx.strokeStyle = css(color);
    ctx.beginPath(); ctx.moveTo(x0, y0); ctx.lineTo(x1, y1); ctx.stroke();
    ctx.lineCap = 'butt';
  }
  function rod(ctx, x0, y0, x1, y1, w0, w1, colors) {
    const dx = x1 - x0, dy = y1 - y0, len = Math.hypot(dx, dy);
    if (len < 0.5) return;
    const nx = -dy / len, ny = dx / len;
    ctx.beginPath();
    ctx.moveTo(x0 + nx * w0 / 2, y0 + ny * w0 / 2);
    ctx.lineTo(x1 + nx * w1 / 2, y1 + ny * w1 / 2);
    ctx.lineTo(x1 - nx * w1 / 2, y1 - ny * w1 / 2);
    ctx.lineTo(x0 - nx * w0 / 2, y0 - ny * w0 / 2);
    ctx.closePath();
    const sgn = (nx * -0.6 + ny * -0.8) > 0 ? 1 : -1;
    const mx = (x0 + x1) / 2, my = (y0 + y1) / 2, hw = Math.max(w0, w1) / 2;
    ctx.fillStyle = stops(ctx.createLinearGradient(mx + nx * hw * sgn, my + ny * hw * sgn, mx - nx * hw * sgn, my - ny * hw * sgn), colors);
    ctx.fill();
  }
  function stickShadow(ctx, bx, by, tx, ty, S, width) {
    const ex = tx + S * 0.07, ey = ty + S * 0.10;
    line(ctx, bx, by, ex, ey, width * 2.0, 0x22000000, 'round');
    line(ctx, bx, by, ex, ey, width * 1.2, 0x40000000, 'round');
  }
  function redCap(ctx, b, rad, down) {
    vgrad(ctx, b, rad, down ? [0xFFC93A2F, 0xFFA8231A, 0xFF7C140E] : [0xFFFF7465, 0xFFE33A2E, 0xFFB01E15], [0, 0.5, 1]);
    ctx.fillStyle = stops(ctx.createLinearGradient(0, b.t, 0, b.t + b.h * 0.35), [down ? 0x30FFFFFF : 0x90FFFFFF, 0x00FFFFFF]);
    rrect(ctx, b.l + rad, b.t + rad * 0.3, b.r - rad, b.t + b.h * 0.35, rad * 0.6);
    ctx.fill();
    ctx.lineWidth = Math.max(1, b.h * 0.03);
    ctx.strokeStyle = css(0x60000000);
    rrect(ctx, b.l, b.t, b.r, b.b, rad);
    ctx.stroke();
  }

  // Radio Shack joystick
  function stdStatic(ctx, g) {
    const S = g.S, body = box(g, 0), rad = S * 0.07;
    dropShadow(ctx, body, rad, S);
    vgrad(ctx, body, rad, [0xFF19191B, 0xFF0C0C0D, 0xFF050506]);
    const top = box(g, S * 0.022);
    dgrad(ctx, top, rad * 0.85, 0xFF2E2E31, 0xFF141416);
    texture(ctx, grain(), top, rad * 0.85, 0x30);
    bevel(ctx, top, rad * 0.85, Math.max(1.5, S * 0.008), 0x50FFFFFF, 0x70000000);
    const sx = g.sx, sy = g.sy, Rr = g.R;
    const gx = sx - Rr * 0.25, gy = sy - Rr * 0.3;
    ctx.fillStyle = stops(ctx.createRadialGradient(gx, gy, 0, gx, gy, Rr * 1.4), [0xFF303034, 0xFF1D1D20, 0xFF141416], [0, 0.6, 1]);
    circle(ctx, sx, sy, Rr * 1.18); ctx.fill();
    const w = Math.max(1.2, S * 0.007);
    for (let i = 0; i < 7; i++) {
      const rr = Rr * (1.18 - i * 0.12);
      ring(ctx, sx + w * 0.7, sy + w, rr, w, 0xB0000000);
      ring(ctx, sx - w * 0.5, sy - w * 0.7, rr, w * 0.8, 0x2CFFFFFF);
    }
    sphere(ctx, sx, sy, S * 0.055, [0xFF2A2A2D, 0xFF0E0E0F, 0xFF050505]);
    ring(ctx, sx, sy, S * 0.055, Math.max(1, S * 0.005), 0x60000000);
    const ty = sy - Rr * 1.30;
    ctx.beginPath(); ctx.moveTo(sx, ty - S * 0.035); ctx.lineTo(sx - S * 0.035, ty + S * 0.02); ctx.lineTo(sx + S * 0.035, ty + S * 0.02); ctx.closePath();
    ctx.fillStyle = css(0xFF0B0B0C); ctx.fill();
    emboss(ctx, 'Radio Shack', g.cx + S * 0.25, g.cy - S * 0.33, S * 0.058, 0xFF232326, true);
    ctx.fillStyle = css(0xFF000000);
    rrect(ctx, g.f1x - S * 0.145, body.t - S * 0.01, g.f1x + S * 0.145, body.t + S * 0.06, S * 0.012); ctx.fill();
  }
  function stdDynamic(ctx, g, x, y, f1) {
    const S = g.S, bt = g.cy - S / 2;
    const b = R(g.f1x - S * 0.13, bt - S * 0.03 + (f1 ? S * 0.008 : 0), g.f1x + S * 0.13, bt + S * 0.05);
    if (!f1) vgrad(ctx, R(b.l, b.t + S * 0.01, b.r + S * 0.008, b.b + S * 0.018), S * 0.012, [0x00000000, 0x90000000]);
    redCap(ctx, b, S * 0.012, f1);
    const tx = topX(g, 'std', x), ty = topY(g, 'std', y);
    stickShadow(ctx, g.sx, g.sy, tx, ty, S, S * 0.05);
    rod(ctx, g.sx, g.sy, tx, ty, S * 0.075, S * 0.058, [0xFF46464A, 0xFF1A1A1C, 0xFF060606, 0xFF000000]);
    const r = S * 0.034;
    sphere(ctx, tx, ty, r, [0xFF5E5E63, 0xFF222224, 0xFF060606], [0, 0.5, 1]);
    sphere(ctx, tx + r * 0.05, ty + r * 0.05, r * 0.42, [0xFF000000, 0xFF0A0A0A, 0xFF2C2C2F], [0, 0.6, 1]);
  }

  // TRS-80 Deluxe Joystick
  function facet(ctx, x0, y0, x1, y1, x2, y2, x3, y3, outer, inner) {
    ctx.beginPath(); ctx.moveTo(x0, y0); ctx.lineTo(x1, y1); ctx.lineTo(x2, y2); ctx.lineTo(x3, y3); ctx.closePath();
    ctx.fillStyle = stops(ctx.createLinearGradient((x0 + x1) / 2, (y0 + y1) / 2, (x2 + x3) / 2, (y2 + y3) / 2), [outer, inner]);
    ctx.fill();
  }
  function trimTab(ctx, x, y, w, h) {
    dgrad(ctx, R(x, y, x + w, y + h), Math.min(w, h) * 0.3, 0xFF4A4A4A, 0xFF151515);
    for (let k = 1; k < 6; k++) {
      if (h > w) line(ctx, x + w * 0.2, y + h * k / 6, x + w * 0.8, y + h * k / 6, 1, 0x40000000);
      else line(ctx, x + w * k / 6, y + h * 0.2, x + w * k / 6, y + h * 0.8, 1, 0x40000000);
    }
  }
  function deluxeStatic(ctx, g) {
    const S = g.S, body = box(g, 0), rad = S * 0.06;
    dropShadow(ctx, body, rad, S);
    vgrad(ctx, body, rad, [0xFFC9C2AE, 0xFFB2AB97, 0xFF948D7A]);
    const top = box(g, S * 0.02);
    dgrad(ctx, top, rad * 0.9, 0xFFE9E3D2, 0xFFCFC8B4);
    texture(ctx, grain(), top, rad * 0.9, 0x46);
    bevel(ctx, top, rad * 0.9, Math.max(1.5, S * 0.008), 0x90FFFFFF, 0x50000000);
    const sx = g.sx, sy = g.sy, o = S * 0.32, i = S * 0.297, inn = S * 0.10;
    const frame = R(sx - o, sy - o, sx + o, sy + o);
    blurred(ctx, S * 0.012, () => {
      ctx.fillStyle = css(0x90000000);
      rrect(ctx, frame.l, frame.t + S * 0.008, frame.r + S * 0.006, frame.b + S * 0.014, S * 0.02); ctx.fill();
    });
    dgrad(ctx, frame, S * 0.02, 0xFF2A2A2A, 0xFF0A0A0A);
    bevel(ctx, frame, S * 0.02, Math.max(1, S * 0.006), 0x40FFFFFF, 0x80000000);
    const l = sx - i, t = sy - i, r = sx + i, b = sy + i;
    facet(ctx, l, t, r, t, sx + inn, sy - inn, sx - inn, sy - inn, 0xFF0D0D0D, 0xFF151515);
    facet(ctx, l, t, l, b, sx - inn, sy + inn, sx - inn, sy - inn, 0xFF101010, 0xFF1A1A1A);
    facet(ctx, r, t, r, b, sx + inn, sy + inn, sx + inn, sy - inn, 0xFF3A3A3A, 0xFF262626);
    facet(ctx, l, b, r, b, sx + inn, sy + inn, sx - inn, sy + inn, 0xFF424242, 0xFF2A2A2A);
    const sw = Math.max(1, S * 0.004);
    line(ctx, l, t, sx - inn, sy - inn, sw, 0x50000000);
    line(ctx, r, t, sx + inn, sy - inn, sw, 0x50000000);
    line(ctx, l, b, sx - inn, sy + inn, sw, 0x50000000);
    line(ctx, r, b, sx + inn, sy + inn, sw, 0x50000000);
    ctx.fillStyle = css(0xFF060606); ctx.fillRect(sx - inn, sy - inn, inn * 2, inn * 2);
    sphere(ctx, sx, sy, inn * 0.85, [0xFF3A3A3C, 0xFF1E1E1F, 0xFF0A0A0A]);
    for (let k = 1; k <= 3; k++) ring(ctx, sx, sy, inn * 0.85 * k / 3.6, sw, 0x30000000);
    trimTab(ctx, frame.l - S * 0.012, sy - S * 0.07, S * 0.022, S * 0.14);
    trimTab(ctx, sx - S * 0.07, frame.b - S * 0.012, S * 0.14, S * 0.022);
    const bx = g.f1x, by = g.f1y, bs = S * 0.075, bez = R(bx - bs, by - bs, bx + bs, by + bs);
    dgrad(ctx, bez, S * 0.006, 0xFF3C3C3C, 0xFF0E0E0E);
    ctx.fillStyle = css(0xFFB8B8B8); ctx.fillRect(bez.l, bez.b - S * 0.018, bez.w, S * 0.018);
    const py = g.cy + S * 0.375, ph = S * 0.095, pl = g.cx - S * 0.43, pm = g.cx - S * 0.05, pr = g.cx + S * 0.43;
    const silver = R(pl, py, pm, py + ph), black = R(pm, py, pr, py + ph);
    vgrad(ctx, silver, 0, [0xFFF2F2F2, 0xFFC4C4C4, 0xFFDCDCDC, 0xFFB0B0B0]);
    texture(ctx, streaks(), silver, 0, 0x30);
    vgrad(ctx, black, 0, [0xFF2A2A2A, 0xFF101010]);
    texture(ctx, streaks(), black, 0, 0x18);
    ctx.lineWidth = sw; ctx.strokeStyle = css(0x80000000); ctx.strokeRect(pl, py, pr - pl, ph);
    text(ctx, 'TRS-80', (pl + pm) / 2, py + ph * 0.68, ph * 0.52, 0xFF1E1E1E);
    text(ctx, 'DELUXE', (pm + pr) / 2, py + ph * 0.43, ph * 0.36, 0xFFF2F2F2);
    text(ctx, 'JOYSTICK', (pm + pr) / 2, py + ph * 0.86, ph * 0.40, 0xFFF2F2F2);
  }
  function deluxeDynamic(ctx, g, x, y, f1) {
    const S = g.S, bs = S * 0.058, d = f1 ? S * 0.006 : 0;
    const b = R(g.f1x - bs, g.f1y - bs - S * 0.008 + d, g.f1x + bs, g.f1y + bs - S * 0.008 + d);
    vgrad(ctx, b, S * 0.008, f1 ? [0xFF9E1C14, 0xFF6E0E09] : [0xFFC2261C, 0xFF7E120B]);
    redCap(ctx, R(b.l + bs * 0.18, b.t + bs * 0.12, b.r - bs * 0.18, b.b - bs * 0.32), S * 0.006, f1);
    const tx = topX(g, 'deluxe', x), ty = topY(g, 'deluxe', y);
    stickShadow(ctx, g.sx, g.sy, tx, ty, S, S * 0.05);
    rod(ctx, g.sx, g.sy, tx, ty, S * 0.06, S * 0.055, [0xFF4A4A4D, 0xFF151516, 0xFF000000]);
    const r = S * 0.04;
    sphere(ctx, tx, ty, r, [0xFF4E4E52, 0xFF1C1C1E, 0xFF050505]);
    for (let k = 0; k < 28; k++) {
      const a = k * Math.PI * 2 / 28, cs = Math.cos(a), sn = Math.sin(a);
      line(ctx, tx + cs * r * 0.68, ty + sn * r * 0.68, tx + cs * r * 0.97, ty + sn * r * 0.97, Math.max(1, S * 0.004), k % 2 === 0 ? 0x50FFFFFF : 0x90000000);
    }
  }

  // Wico
  function wicoStatic(ctx, g) {
    const S = g.S, body = box(g, 0), rad = S * 0.16;
    dropShadow(ctx, body, rad, S);
    vgrad(ctx, R(body.l, body.t + S * 0.03, body.r, body.b), rad, [0xFFB8261A, 0xFFD43426, 0xFF7E1209], [0, 0.7, 1]);
    const casing = R(body.l, body.t, body.r, body.b - S * 0.03);
    vgrad(ctx, casing, rad, [0xFF202022, 0xFF0E0E0F]);
    const top = R(casing.l + S * 0.018, casing.t + S * 0.015, casing.r - S * 0.018, casing.b - S * 0.03);
    dgrad(ctx, top, rad * 0.9, 0xFF36363A, 0xFF19191B);
    texture(ctx, grain(), top, rad * 0.9, 0x34);
    bevel(ctx, top, rad * 0.9, Math.max(1.5, S * 0.008), 0x40FFFFFF, 0x80000000);
    const sx = g.sx, sy = g.sy, Rr = g.R;
    blurred(ctx, S * 0.02, () => { ctx.fillStyle = css(0xA0000000); circle(ctx, sx + S * 0.01, sy + S * 0.02, Rr * 1.28); ctx.fill(); });
    sphere(ctx, sx, sy, Rr * 1.28, [0xFF414146, 0xFF1F1F22, 0xFF0C0C0D], [0, 0.55, 1]);
    ring(ctx, sx, sy, Rr * 1.28, Math.max(1, S * 0.006), 0x70000000);
    for (let k = 0; k < 36; k++) {
      const a = k * Math.PI * 2 / 36, cs = Math.cos(a), sn = Math.sin(a);
      const x0 = sx + cs * Rr * 1.02, y0 = sy + sn * Rr * 1.02, x1 = sx + cs * Rr * 1.15, y1 = sy + sn * Rr * 1.15;
      const w = Math.max(1.2, S * 0.012);
      line(ctx, x0 + 1, y0 + 1.5, x1 + 1, y1 + 1.5, w, 0x90000000, 'round');
      line(ctx, x0, y0, x1, y1, w, 0x30FFFFFF, 'round');
    }
    sphere(ctx, sx, sy, Rr * 0.85, [0xFF0C0C0D, 0xFF1B1B1E, 0xFF2E2E32], [0, 0.6, 1]);
    sphere(ctx, sx, sy, S * 0.05, [0xFF9A9A98, 0xFF4A4A4A, 0xFF1A1A1A]);
    blurred(ctx, S * 0.012, () => { ctx.fillStyle = css(0x90000000); circle(ctx, g.f1x + S * 0.008, g.f1y + S * 0.014, S * 0.095); ctx.fill(); });
    sphere(ctx, g.f1x, g.f1y, S * 0.095, [0xFF38383C, 0xFF151517, 0xFF0A0A0B]);
    const ly = g.cy + S * 0.415;
    text(ctx, 'BASE', g.cx - S * 0.25, ly + S * 0.012, S * 0.04, 0xFF8A8A88);
    text(ctx, 'STICK', g.cx, ly + S * 0.012, S * 0.04, 0xFF8A8A88);
    const slot = R(g.cx - S * 0.165, ly - S * 0.02, g.cx - S * 0.085, ly + S * 0.016);
    ctx.fillStyle = css(0xFF050505); rrect(ctx, slot.l, slot.t, slot.r, slot.b, S * 0.006); ctx.fill();
    dgrad(ctx, R(slot.l + S * 0.005, slot.t + S * 0.004, slot.l + S * 0.04, slot.b - S * 0.004), S * 0.004, 0xFF6A6A6A, 0xFF2A2A2A);
    text(ctx, 'BUTTON SELECTOR', g.cx + S * 0.27, ly + S * 0.010, S * 0.03, 0xFF7A7A78);
  }
  function wicoDynamic(ctx, g, x, y, f1, f2) {
    const S = g.S, br0 = S * 0.062 * (f1 ? 0.96 : 1);
    sphere(ctx, g.f1x, g.f1y + (f1 ? S * 0.004 : 0), br0, f1 ? [0xFFE0574A, 0xFFB0241A, 0xFF6A0E08] : [0xFFFF8C7E, 0xFFE8392C, 0xFF8E160E], [0, 0.5, 1]);
    specular(ctx, g.f1x - br0 * 0.35, g.f1y - br0 * 0.4, br0 * 0.45, f1 ? 0x50 : 0xB0);
    const bx = topX(g, 'wico', x), by = topY(g, 'wico', y), br = S * 0.15;
    softShadow(ctx, bx + S * 0.07, by + S * 0.10, br * 1.3, 0x90);
    rod(ctx, g.sx, g.sy, bx, by, S * 0.045, S * 0.04, [0xFF3A3A3A, 0xFFEDEDED, 0xFF8A8A8A, 0xFF2A2A2A]);
    sphere(ctx, bx, by, br, [0xFFFFB0A4, 0xFFEC3F31, 0xFFB81C12, 0xFF4E0603], [0, 0.22, 0.62, 1]);
    specular(ctx, bx - br * 0.42, by - br * 0.48, br * 0.38, 0xD0);
    const tx = tipX(g, x), ty = tipY(g, y), tr = S * 0.052 * (f2 ? 0.93 : 1);
    softShadow(ctx, tx + tr * 0.25, ty + tr * 0.35, tr * 1.35, 0x70);
    sphere(ctx, tx, ty, tr, f2 ? [0xFFDAD4C6, 0xFFB9B2A2, 0xFF8E8778] : [0xFFFFFFFF, 0xFFF2EEE4, 0xFFBDB6A6], [0, 0.5, 1]);
  }

  return {
    TYPES: ['std', 'deluxe', 'wico'],
    selfCentering: (type) => type !== 'std',
    layout, topX, topY, tipX, tipY,
    drawStatic(ctx, type, g) { (type === 'deluxe' ? deluxeStatic : type === 'wico' ? wicoStatic : stdStatic)(ctx, g); },
    drawDynamic(ctx, type, g, x, y, f1, f2) {
      if (type === 'deluxe') deluxeDynamic(ctx, g, x, y, f1);
      else if (type === 'wico') wicoDynamic(ctx, g, x, y, f1, f2);
      else stdDynamic(ctx, g, x, y, f1);
    },
  };
})();

globalThis.JoyArt = JoyArt;
