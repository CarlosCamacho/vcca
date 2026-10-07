// Front end for the Android port of VCC. Runs the VCC core (vcc.wasm),
// draws its frames, plays its audio, and feeds it keys, joystick, ROMs and
// disk images. Works in Android's WebView and in a desktop browser.
'use strict';

const $ = (id) => document.getElementById(id);
const FRAME_MS = 1000 / 59.923;   // the CoCo's field rate

// ---------------------------------------------------------------- storage

const Store = {
  db: null,
  open() {
    return new Promise((resolve) => {
      let req;
      try { req = indexedDB.open('vcc', 1); } catch (e) { resolve(null); return; }
      req.onupgradeneeded = () => req.result.createObjectStore('files');
      req.onsuccess = () => { this.db = req.result; resolve(this.db); };
      req.onerror = () => resolve(null);
    });
  },
  get(key) {
    return new Promise((resolve) => {
      if (!this.db) return resolve(null);
      const r = this.db.transaction('files').objectStore('files').get(key);
      r.onsuccess = () => resolve(r.result || null);
      r.onerror = () => resolve(null);
    });
  },
  put(key, value) {
    return new Promise((resolve) => {
      if (!this.db) return resolve(false);
      const tx = this.db.transaction('files', 'readwrite');
      tx.objectStore('files').put(value, key);
      tx.oncomplete = () => resolve(true);
      tx.onerror = () => resolve(false);
    });
  },
  del(key) {
    return new Promise((resolve) => {
      if (!this.db) return resolve(false);
      const tx = this.db.transaction('files', 'readwrite');
      tx.objectStore('files').delete(key);
      tx.oncomplete = () => resolve(true);
      tx.onerror = () => resolve(false);
    });
  },
};

const VERSION = '1.2.0';
const VCC_VERSION = '2.1.10.0';   // the VCC source the core is built from (its Vcc.rc)
const defaults = {
  cpu: 0, ram: 1, rgb: 1, scan: 0, throttle: 1, overclock: 0, touchJoy: 0, turboDisk: 0,
  drives: [null, null, null, null],   // {key, name}
  mpi: 1, switchSlot: 3, sticky: 1, hires: 0, diskRom: 'disk11',
  hdd: [null, null],                   // IDE master/slave: {disks, labels: {n: name}}
  // Multi-Pak slots: {type, key, name}; type 0 empty, 1 FD-502, 2 ROM pak,
  // 3 Orchestra-90, 4 Speech/Sound Pak, 5 Game Master Cartridge.
  slots: [null, null, null, { type: 1 }],
  panel: 'kbd', showControls: true,
  joyCenter: 1,          // the on-screen stick springs back to the middle
  joySide: 'left',       // stick on the left, fire buttons on the right; 'right' swaps
  picture: 'full',       // full (with border), title, zoomed
  cmdSlots: {},          // Commands tab user slots 10-16: text, | means ENTER
  joyPorts: null,        // {left, right}: {src: none|stick|touch|keys, emu: 0|2|3}
  prnLF: 1,              // BitBanger: add LF after CR
  tape: null,            // {key, name, kind}: the tape in the deck
  tapeFast: 1,           // CAS fast load, and full speed while the tape plays
  gime86: 0,             // GIME chip: 0 = 1987, 1 = 1986 (timer one line later)
};
let settings = Object.assign({}, defaults);
// Settings are kept in Android's app preferences (SharedPreferences) and in
// the WebView's own storage; either restores them on the next start.
function loadSettings() {
  let json = null;
  try { if (window.AndroidHost && window.AndroidHost.getPrefs) json = window.AndroidHost.getPrefs(); } catch (e) { /* no bridge */ }
  try { if (!json) json = localStorage.getItem('vcc-settings'); } catch (e) { /* storage unavailable */ }
  try { settings = Object.assign({}, defaults, JSON.parse(json || '{}')); } catch (e) { settings = Object.assign({}, defaults); }
}
// Settings saved before the per-port joystick configuration: the stick was
// the right port, touch drove the right port too, one hi-res type for both.
function migrateSettings() {
  if (!settings.joyPorts) {
    const emu = +settings.hires || 0;
    settings.joyPorts = { left: { src: 'none', emu }, right: { src: +settings.touchJoy ? 'touch' : 'stick', emu } };
  }
  settings.joyPorts = JSON.parse(JSON.stringify(settings.joyPorts));
}
function saveSettings() {
  const json = JSON.stringify(settings);
  try { localStorage.setItem('vcc-settings', json); } catch (e) { /* ignore */ }
  try { if (window.AndroidHost && window.AndroidHost.setPrefs) window.AndroidHost.setPrefs(json); } catch (e) { /* ignore */ }
}

function toast(msg, ms = 2200) {
  const t = $('toast');
  t.textContent = msg;
  t.style.display = 'block';
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => { t.style.display = 'none'; }, ms);
}

// ---------------------------------------------------------------- wasm

let vcc = null, mem = null, wasmId = '';
const enc = new TextEncoder();

function wasiImports() {
  const wasi = {
    fd_write(fd, iovs, n, nwritten) {
      const dv = new DataView(mem.buffer);
      let total = 0;
      for (let i = 0; i < n; i++) total += dv.getUint32(iovs + i * 8 + 4, true);
      dv.setUint32(nwritten, total, true);
      return 0;
    },
    clock_time_get(id, precision, out) {
      const ns = id === 0 ? BigInt(Date.now()) * 1000000n : BigInt(Math.round(performance.now() * 1e6));
      new DataView(mem.buffer).setBigUint64(out, ns, true);
      return 0;
    },
    random_get(ptr, len) {
      crypto.getRandomValues(new Uint8Array(mem.buffer, ptr, len));
      return 0;
    },
    proc_exit(code) { throw new Error('VCC exited: ' + code); },
  };
  return {
    wasi_snapshot_preview1: new Proxy(wasi, { get: (t, k) => t[k] || (() => 0) }),
    vcca: fileImports(),
  };
}

// ---------------------------------------------------------------- disk files

// Disk and hard drive images the emulator opens by name ("drive0".."drive3",
// "ide0", "ide1"). Their bytes live here, not in WebAssembly memory, so a
// snapshot (a copy of that memory) stays small and never rolls back a disk.
// A name keeps the same id forever, so handles inside a restored snapshot
// still point at the right file.
const Files = new Map();   // name -> { data, readOnly, dirty, chunks: Set }
const fileIdOf = new Map();
const fileNames = [];
const HDD_CHUNK = 256 * 1024;
function fileId(name) {
  if (!fileIdOf.has(name)) { fileIdOf.set(name, fileNames.length); fileNames.push(name); }
  return fileIdOf.get(name);
}
function putFile(name, data, readOnly) { Files.set(name, { data, readOnly: !!readOnly, dirty: false, chunks: new Set() }); }

function fileImports() {
  const dec = new TextDecoder('latin1');
  const get = (id) => Files.get(fileNames[id]);
  return {
    file_open(namePtr, len, write) {
      const name = dec.decode(new Uint8Array(mem.buffer, namePtr, len));
      const f = Files.get(name);
      if (!f || (write && f.readOnly)) return -1;
      return fileId(name);
    },
    file_size(id) { const f = get(id); return f ? f.data.length : 0; },
    file_read(id, pos, buf, n) {
      const f = get(id);
      pos >>>= 0; n >>>= 0;
      if (!f || pos >= f.data.length) return 0;
      const count = Math.min(n, f.data.length - pos);
      new Uint8Array(mem.buffer, buf, count).set(f.data.subarray(pos, pos + count));
      return count;
    },
    file_write(id, pos, buf, n) {
      const f = get(id);
      pos >>>= 0; n >>>= 0;
      if (!f || f.readOnly) return 0;
      if (pos + n > f.data.length) { const grown = new Uint8Array(pos + n); grown.set(f.data); f.data = grown; }
      f.data.set(new Uint8Array(mem.buffer, buf, n), pos);
      f.dirty = true;
      for (let c = Math.floor(pos / HDD_CHUNK); c <= Math.floor((pos + n - 1) / HDD_CHUNK); c++) f.chunks.add(c);
      return n;
    },
    file_close() {},
  };
}

async function loadWasm() {
  const resp = await fetch('vcc.wasm');
  const bytes = await resp.arrayBuffer();
  wasmId = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)).slice(0, 8)).map((b) => b.toString(16).padStart(2, '0')).join('');
  const { instance } = await WebAssembly.instantiate(bytes, wasiImports());
  vcc = instance.exports;
  mem = vcc.memory;
  vcc._initialize();
}

// Copies bytes into wasm memory; the caller frees with vcc.vcc_free.
function toWasm(bytes) {
  const p = vcc.vcc_alloc(bytes.length + 1);
  const u = new Uint8Array(mem.buffer, p, bytes.length + 1);
  u.set(bytes);
  u[bytes.length] = 0;
  return p;
}
function withBytes(bytes, fn) { const p = toWasm(bytes); try { return fn(p, bytes.length); } finally { vcc.vcc_free(p); } }
function withStr(s, fn) { return withBytes(enc.encode(s), fn); }
function cstr(p) {
  const u = new Uint8Array(mem.buffer);
  let e = p;
  while (u[e]) e++;
  return new TextDecoder('latin1').decode(u.subarray(p, e));
}

// ---------------------------------------------------------------- machine

let haveRom = false, haveDiskRom = false, running = false, paused = false;

const CART_TYPES = ['Empty', 'FD-502 disk controller', 'Program pak (ROM)', 'Orchestra-90 CC', 'Speech/Sound Pak', 'Game Master Cartridge', 'Stereo Composer', 'Symphony 12', 'CoCo PSG', 'Game Master Cartridge, sound only (disk games)', 'IDE hard drive interface ($FF70)', 'IDE hard drive interface ($FF50)'];
// Menu entries that are another cartridge type with no ROM.
const NATIVE_TYPE = { 9: 5 };
const NEEDS_FILE = { 2: true, 5: true };   // types whose ROM comes with the slot

// Some ROM dumps start with a 2-byte little-endian load address (00 80 = $8000 for the
// CoCo 3 ROM, $C000 for a cartridge), e.g. a 32,770-byte coco3.rom. VCC wants
// the raw image, and with the header left on, every byte is two places off
// and the reset vector points into nowhere.
function stripLoadHeader(bytes) {
  if (bytes.length % 1024 === 2) {
    const addr = bytes[0] | (bytes[1] << 8);
    if (addr >= 0x8000 && (addr & 0x1fff) === 0) return bytes.subarray(2);
  }
  return bytes;
}

async function getBytes(key) {
  const data = key ? await Store.get(key) : null;
  return data ? stripLoadHeader(new Uint8Array(data)) : null;
}

async function applyRoms() {
  const rom = await getBytes('rom:coco3');
  haveRom = !!rom;
  haveDiskRom = !!(await Store.get('rom:disk11')) || !!(await Store.get('rom:hdbdos'));
  if (rom) withBytes(rom, (p, n) => vcc.vcc_set_rom(p, n));
  vcc.vcc_multipak(+settings.mpi, +settings.switchSlot);
  for (let i = 0; i < 4; i++) {
    const slot = settings.slots[i];
    const type = slot ? +slot.type : 0;
    const native = NATIVE_TYPE[type] || type;
    let a = null, b = null;
    if (type === 1) a = (settings.diskRom === 'hdbdos' && await getBytes('rom:hdbdos')) || await getBytes('rom:disk11');
    else if (type === 3) a = await getBytes('rom:orch90');
    else if (type === 4) { a = await getBytes('rom:ssc-pic'); b = await getBytes('rom:ssc-spo'); }
    else if (type === 8) a = await getBytes('rom:psg');   // optional menu firmware
    else if (NEEDS_FILE[type]) a = await getBytes(slot.key);
    if (type === 1 && !a) a = new Uint8Array(0);
    const pa = a ? toWasm(a) : 0, pb = b ? toWasm(b) : 0;
    vcc.vcc_insert_cart(i, NEEDS_FILE[type] && !a ? 0 : native, pa, a ? a.length : 0, pb, b ? b.length : 0);
    if (pa) vcc.vcc_free(pa);
    if (pb) vcc.vcc_free(pb);
  }
  updateRomLabels();
}

function applyConfig() {
  vcc.vcc_configure(+settings.ram, +settings.cpu, +settings.rgb, 0, +settings.overclock);   // scan lines: applyScanlines
  vcc.vcc_turbo_disk(settings.turboDisk ? 1 : 0);
  vcc.vcc_hires_ports(+settings.joyPorts.left.emu, +settings.joyPorts.right.emu);
  vcc.vcc_tape_fastload(+settings.tapeFast);
  vcc.vcc_gime86(+settings.gime86);
}

async function mountDrives() {
  for (let d = 0; d < 4; d++) {
    const info = settings.drives[d];
    if (!info) continue;
    const data = await Store.get(info.key);
    if (!data) { settings.drives[d] = null; continue; }
    mountBytes(d, info, new Uint8Array(data));
  }
  saveSettings();
  renderDrives();
}

function mountBytes(drive, info, bytes) {
  const name = 'drive' + drive;
  putFile(name, bytes.slice());
  withStr(name, (np) => { if (!vcc.vcc_mount_disk(drive, np)) toast('Could not mount ' + info.name); });
}

// Disk writes land in wasm memory; copy changed images back to storage.
async function saveDirtyDisks() {
  if (!vcc) return;
  await saveHardDrives();
  await saveTape();
  for (let d = 0; d < 4; d++) {
    const info = settings.drives[d];
    if (!info) continue;
    const f = Files.get('drive' + d);
    if (f && f.dirty) {
      f.dirty = false;
      f.chunks.clear();
      await Store.put(info.key, f.data.slice().buffer);
      if (!info.modified) { info.modified = true; saveSettings(); }
    }
  }
}

// ---------------------------------------------------------------- hard drives

// An IDE image is the file "ide0"/"ide1" and lives in IndexedDB as 256K
// chunks, so a write saves only the chunks it touched. HDB-DOS keeps virtual
// floppy n at LSN n * 630, each 256-byte sector in the first half of a
// 512-byte IDE sector.
const HDD_SECTORS = 630;
const HDD_DISK_BYTES = HDD_SECTORS * 512;   // one virtual floppy
const hddKey = (d, i) => `hdd:${d}:${i}`;

async function loadHardDrives() {
  for (let d = 0; d < 2; d++) {
    const info = settings.hdd[d];
    if (!info) continue;
    const data = new Uint8Array(info.disks * HDD_DISK_BYTES);
    const chunks = Math.ceil(data.length / HDD_CHUNK);
    for (let i = 0; i < chunks; i++) {
      const c = await Store.get(hddKey(d, i));
      if (c) data.set(new Uint8Array(c), i * HDD_CHUNK);
    }
    putFile('ide' + d, data);
  }
}

async function saveHardDrives() {
  for (let d = 0; d < 2; d++) {
    const f = Files.get('ide' + d);
    if (!settings.hdd[d] || !f || !f.chunks.size) continue;
    const list = Array.from(f.chunks);
    f.chunks.clear();
    for (const i of list) await Store.put(hddKey(d, i), f.data.slice(i * HDD_CHUNK, (i + 1) * HDD_CHUNK).buffer);
  }
}

function markAllChunks(f) { for (let i = 0; i < Math.ceil(f.data.length / HDD_CHUNK); i++) f.chunks.add(i); }

// A new drive: every virtual floppy an empty, already-formatted RS-DOS disk.
function newHddImage(disks) {
  const data = new Uint8Array(disks * HDD_DISK_BYTES);
  for (let s = 0; s < disks * HDD_SECTORS; s++) data.fill(0xff, s * 512, s * 512 + 256);
  return data;
}
function hddPutDisk(data, n, dsk) {
  for (let s = 0; s < HDD_SECTORS; s++) {
    const at = (n * HDD_SECTORS + s) * 512;
    if ((s + 1) * 256 <= dsk.length) data.set(dsk.subarray(s * 256, (s + 1) * 256), at);
    else data.fill(0xff, at, at + 256);
  }
}
function hddGetDisk(data, n) {
  const out = new Uint8Array(HDD_SECTORS * 256);
  for (let s = 0; s < HDD_SECTORS; s++) out.set(data.subarray((n * HDD_SECTORS + s) * 512, (n * HDD_SECTORS + s) * 512 + 256), s * 256);
  return out;
}

async function createHardDrive(d) {
  const disks = +$('selHddSize').value;
  if (settings.hdd[d] && !confirm(`Replace the ${d ? 'slave' : 'master'} hard drive? Everything on it will be erased.`)) return;
  await removeHardDrive(d, true);
  putFile('ide' + d, newHddImage(disks));
  markAllChunks(Files.get('ide' + d));
  settings.hdd[d] = { disks, labels: {} };
  saveSettings();
  toast('Saving the new hard drive…', 4000);
  await saveHardDrives();
  await hardReset();
  renderHdds();
  toast(`${d ? 'Slave' : 'Master'} hard drive created: ${disks} empty virtual floppies`);
}

async function removeHardDrive(d, quiet) {
  const info = settings.hdd[d];
  if (!info) return;
  if (!quiet && !confirm(`Remove the ${d ? 'slave' : 'master'} hard drive and everything on it?`)) return;
  const chunks = Math.ceil(info.disks * HDD_DISK_BYTES / HDD_CHUNK);
  for (let i = 0; i < chunks; i++) await Store.del(hddKey(d, i));
  Files.delete('ide' + d);
  settings.hdd[d] = null;
  saveSettings();
  if (!quiet) { await hardReset(); renderHdds(); }
}

async function addDisksToHardDrive(d) {
  const info = settings.hdd[d];
  const files = await pickFiles();
  if (!files || !files.length) return;
  let first = 0;
  while (info.labels[first] !== undefined && first < info.disks) first++;
  const answer = prompt(`Put ${files.length === 1 ? 'it' : 'the first one'} in virtual floppy number (0-${info.disks - 1}):`, String(first));
  if (answer === null) return;
  let n = parseInt(answer, 10);
  if (!(n >= 0 && n < info.disks)) { toast('No such virtual floppy'); return; }
  let added = 0;
  for (const f of files) {
    if (n >= info.disks) { toast('The hard drive is full'); break; }
    const bytes = f.bytes.length % 256 ? f.bytes.subarray(f.bytes.length % 256) : f.bytes;   // drop a JVC header
    const file = Files.get('ide' + d);
    hddPutDisk(file.data, n, bytes);
    for (let c = Math.floor(n * HDD_DISK_BYTES / HDD_CHUNK); c <= Math.floor(((n + 1) * HDD_DISK_BYTES - 1) / HDD_CHUNK); c++) file.chunks.add(c);
    info.labels[n] = f.name;
    added++;
    n++;
  }
  saveSettings();
  await saveHardDrives();
  renderHdds();
  toast(`${added} disk${added === 1 ? '' : 's'} added`);
}

async function exportVirtualFloppy(d) {
  const info = settings.hdd[d];
  const answer = prompt(`Save which virtual floppy as a .dsk (0-${info.disks - 1})?`, '0');
  if (answer === null) return;
  const n = parseInt(answer, 10);
  if (!(n >= 0 && n < info.disks)) { toast('No such virtual floppy'); return; }
  saveToDevice(info.labels[n] || `virtual${n}.dsk`, hddGetDisk(Files.get('ide' + d).data, n));
}

async function importHardDrive(d) {
  const f = await pickFile('');
  if (!f) return;
  const disks = Math.floor(f.bytes.length / HDD_DISK_BYTES);
  if (disks < 1) { toast('That file is too small to be an HDB-DOS hard drive image'); return; }
  if (settings.hdd[d] && !confirm('Replace the existing hard drive?')) return;
  await removeHardDrive(d, true);
  putFile('ide' + d, f.bytes.slice(0, disks * HDD_DISK_BYTES));
  markAllChunks(Files.get('ide' + d));
  settings.hdd[d] = { disks, labels: {} };
  saveSettings();
  await saveHardDrives();
  await hardReset();
  renderHdds();
  toast(`Imported ${f.name}: ${disks} virtual floppies`);
}

function exportHardDrive(d) {
  saveToDevice(`vcca-${d ? 'slave' : 'master'}.img`, Files.get('ide' + d).data);
}

function renderHdds() {
  const box = $('hdds');
  box.innerHTML = '';
  for (let d = 0; d < 2; d++) {
    const info = settings.hdd[d];
    const div = document.createElement('div');
    div.className = 'hdd';
    const title = d ? 'Slave' : 'Master';
    if (!info) {
      div.innerHTML = `<b>${title}:</b> none `;
      const mk = document.createElement('button'); mk.textContent = 'Create'; mk.onclick = () => createHardDrive(d);
      const im = document.createElement('button'); im.textContent = 'Import image…'; im.onclick = () => importHardDrive(d);
      div.append(mk, ' ', im);
    } else {
      const used = Object.keys(info.labels).length;
      const mb = (info.disks * HDD_DISK_BYTES / 1048576).toFixed(1);
      div.innerHTML = `<b>${title}:</b> ${info.disks} virtual floppies (${mb} MB), ${used} added here`;
      const list = document.createElement('div');
      list.className = 'vlist';
      list.textContent = Object.keys(info.labels).sort((a, b) => a - b).map((n) => `${n}: ${info.labels[n]}`).join('\n') || 'All empty. Add .dsk files, or save to them from BASIC.';
      list.style.whiteSpace = 'pre-line';
      div.appendChild(list);
      const row = document.createElement('div');
      row.className = 'row';
      for (const [label, fn] of [['Add .dsk files…', addDisksToHardDrive], ['Save a floppy…', exportVirtualFloppy], ['Save image…', exportHardDrive], ['Remove', removeHardDrive]]) {
        const b = document.createElement('button'); b.textContent = label; b.onclick = () => fn(d); row.appendChild(b);
      }
      div.appendChild(row);
    }
    box.appendChild(div);
  }
}

async function bootMachine() {
  vcc.vcc_init();
  await loadHardDrives();
  await applyRoms();
  applyConfig();
  await mountDrives();
  await mountTape();
  vcc.vcc_reset(1);
  running = haveRom;
  if (!haveRom) showWelcome();
}

async function hardReset() {
  await saveDirtyDisks();
  await applyRoms();
  applyConfig();
  vcc.vcc_reset(1);
  running = haveRom;
}

// ---------------------------------------------------------------- video + loop

const canvas = $('screen');
const ctx = canvas.getContext('2d', { alpha: false });
const image = ctx.createImageData(640, 480);
// Picture area, as XRoar offers: the whole frame, a thinner border, or just
// the 512x384 active area.
const PICTURES = { full: [0, 0, 640, 480], title: [32, 24, 576, 432], zoomed: [64, 48, 512, 384] };
// Scan lines. VCC's own blank every other row of its 480-row picture, and
// once a screen scales that to some other height the rows land unevenly:
// stripes of different widths and brightness. Instead this draws a strip with
// one value per physical pixel row, a smooth dip between each of the CoCo's
// lines, and multiplies it over the picture. Where a line is under 2.5 pixels
// tall there is no room to draw it evenly, so it is left off.
function applyScanlines() {
  const el = $('scan'), level = +settings.scan;
  const [, , w, h] = PICTURES[settings.picture] || PICTURES.full;
  const wrap = $('screenWrap');
  const scale = Math.min(wrap.clientWidth / w, wrap.clientHeight / h);
  const cw = w * scale, ch = h * scale, dpr = window.devicePixelRatio || 1;
  const rows = Math.round(ch * dpr), pitch = rows / (h / 2);   // VCC draws each CoCo line twice
  if (!level || !rows || pitch < 2.5) { el.style.display = 'none'; return; }
  const depth = level === 2 ? 0.55 : 0.3;
  el.width = 1; el.height = rows;
  const g = el.getContext('2d'), img = g.createImageData(1, rows);
  for (let y = 0; y < rows; y++) {
    const f = ((y + 0.5) / pitch) % 1;
    const v = Math.round(255 * (1 - depth * (0.5 + 0.5 * Math.cos(2 * Math.PI * f))));
    img.data.set([v, v, v, 255], y * 4);
  }
  g.putImageData(img, 0, 0);
  Object.assign(el.style, { display: 'block', left: `${(wrap.clientWidth - cw) / 2}px`, top: `${(wrap.clientHeight - ch) / 2}px`, width: `${cw}px`, height: `${rows / dpr}px` });
}

// Menu and help sections: opening one closes the others, so the list stays short.
function wireAccordions() {
  for (const box of [$('menuPanel'), document.querySelector('#helpDlg .box')]) {
    const secs = box.querySelectorAll('details.sec');
    for (const d of secs) {
      if (box.id === 'menuPanel' && settings.menuSection) d.open = d.dataset.sec === settings.menuSection;
      d.addEventListener('toggle', () => {
        if (!d.open) return;
        for (const o of secs) if (o !== d) o.open = false;
        if (box.id === 'menuPanel') { settings.menuSection = d.dataset.sec; saveSettings(); if (d.dataset.sec === 'tape') renderTape(); }
        setTimeout(() => d.scrollIntoView({ block: 'nearest', behavior: 'smooth' }), 0);
      });
    }
  }
}

// The bug report form on GitHub, with the version and device filled in.
// Issue forms take a field's value from a query parameter named by its id.
const BUG_FORM = 'https://github.com/CarlosCamacho/vcca/issues/new?template=bug_report.yml';
function bugReportUrl() {
  const m = navigator.userAgent.match(/Android ([^;)]+);\s*([^;)]+)/);
  let device = '';
  if (m) {
    const model = m[2].replace(/\s*Build\/.*$/, '').trim();
    device = (model && model !== 'K' && model !== 'wv' ? model + ', ' : '') + 'Android ' + m[1].trim();
  }
  return `${BUG_FORM}&version=${encodeURIComponent(VERSION)}` + (device ? `&device=${encodeURIComponent(device)}` : '');
}

function openHelp() { $('helpDlg').classList.add('open'); }

function applyPicture() {
  const [, , w, h] = PICTURES[settings.picture] || PICTURES.full;
  canvas.width = w; canvas.height = h;
  applyScanlines();
  if (vcc) blit();
}
let lastTime = 0, acc = 0, statusTimer = 0;

function blit() {
  const src = new Uint8ClampedArray(mem.buffer, vcc.vcc_frame(), 640 * 480 * 4);
  image.data.set(src);
  const [x, y] = PICTURES[settings.picture] || PICTURES.full;
  ctx.putImageData(image, -x, -y);
}

function tick(now) {
  requestAnimationFrame(tick);
  if (!vcc || !running || paused || appPaused) { lastTime = now; return; }
  pollGamepad();
  let frames;
  const tape = tapeInfo();
  const fast = +settings.throttle === 0 || vcc.vcc_unthrottled() || (+settings.tapeFast && tape.mode === 1 && tape.motor);
  if (fast) {
    frames = 8;                         // unthrottled: as many as fit
    acc = 0;
  } else {
    acc += Math.min(now - (lastTime || now), 100);
    frames = Math.floor(acc / FRAME_MS);
    acc -= frames * FRAME_MS;
    if (frames > 4) frames = 4;
  }
  lastTime = now;
  let drew = false;
  const start = performance.now();
  for (let i = 0; i < frames; i++) {
    if (vcc.vcc_run_frame()) drew = true;
    releaseHeldKeys();
    tapeAutoRunFrame();
    pumpAudio(fast);
    if (fast && performance.now() - start > 14) break;
  }
  if (drew) blit();
  if (now - statusTimer > 500) {
    statusTimer = now;
    $('status').textContent = cstr(vcc.vcc_status());
    if ($('menu').classList.contains('open')) { renderTape(); renderPrinter(); }
  }
}

// ---------------------------------------------------------------- audio

let audioCtx = null, audioNode = null, dcL = 0, dcR = 0, lastL = 0, lastR = 0;

async function startAudio() {
  if (audioCtx) { if (audioCtx.state !== 'running') audioCtx.resume(); return; }
  try {
    const AC = window.AudioContext || window.webkitAudioContext;
    audioCtx = new AC({ sampleRate: 44100, latencyHint: 'interactive' });
    await audioCtx.audioWorklet.addModule('audio-worklet.js');
    audioNode = new AudioWorkletNode(audioCtx, 'vcc-audio', { outputChannelCount: [2] });
    audioNode.connect(audioCtx.destination);
    if (audioCtx.state !== 'running') await audioCtx.resume();
  } catch (e) {
    audioNode = null;
    console.warn('audio unavailable', e);
  }
}

function pumpAudio(discard) {
  const n = vcc.vcc_audio_count();
  if (!n) return;
  if (discard || !audioNode || audioCtx.state !== 'running') { vcc.vcc_audio_clear(); return; }
  const src = new Uint32Array(mem.buffer, vcc.vcc_audio(), n);
  const out = new Float32Array(n * 2);
  // Samples are unsigned 16-bit per channel; remove DC with a one-pole filter.
  for (let i = 0; i < n; i++) {
    const s = src[i];
    const l = (s & 0xffff) / 32768, r = (s >>> 16) / 32768;
    dcL = l - lastL + 0.995 * dcL; lastL = l;
    dcR = r - lastR + 0.995 * dcR; lastR = r;
    out[i * 2] = Math.max(-1, Math.min(1, dcL));
    out[i * 2 + 1] = Math.max(-1, Math.min(1, dcR));
  }
  vcc.vcc_audio_clear();
  audioNode.port.postMessage(out, [out.buffer]);
}

// ---------------------------------------------------------------- physical keyboard

// KeyboardEvent.code -> DirectInput scan code, which VCC's keyboard tables use.
const DIK = {
  Escape: 0x01, Digit1: 0x02, Digit2: 0x03, Digit3: 0x04, Digit4: 0x05, Digit5: 0x06, Digit6: 0x07,
  Digit7: 0x08, Digit8: 0x09, Digit9: 0x0A, Digit0: 0x0B, Minus: 0x0C, Equal: 0x0D, Backspace: 0x0E,
  Tab: 0x0F, KeyQ: 0x10, KeyW: 0x11, KeyE: 0x12, KeyR: 0x13, KeyT: 0x14, KeyY: 0x15, KeyU: 0x16,
  KeyI: 0x17, KeyO: 0x18, KeyP: 0x19, BracketLeft: 0x1A, BracketRight: 0x1B, Enter: 0x1C,
  ControlLeft: 0x1D, KeyA: 0x1E, KeyS: 0x1F, KeyD: 0x20, KeyF: 0x21, KeyG: 0x22, KeyH: 0x23,
  KeyJ: 0x24, KeyK: 0x25, KeyL: 0x26, Semicolon: 0x27, Quote: 0x28, Backquote: 0x29,
  ShiftLeft: 0x2A, Backslash: 0x2B, KeyZ: 0x2C, KeyX: 0x2D, KeyC: 0x2E, KeyV: 0x2F, KeyB: 0x30,
  KeyN: 0x31, KeyM: 0x32, Comma: 0x33, Period: 0x34, Slash: 0x35, ShiftRight: 0x36,
  NumpadMultiply: 0x37, AltLeft: 0x38, Space: 0x39, CapsLock: 0x3A, F1: 0x3B, F2: 0x3C,
  F3: 0x3D, F4: 0x3E, F5: 0x3F, F6: 0x40, F7: 0x41, F8: 0x42, F9: 0x43, F10: 0x44,
  NumLock: 0x45, ScrollLock: 0x46, Numpad7: 0x47, Numpad8: 0x48, Numpad9: 0x49,
  NumpadSubtract: 0x4A, Numpad4: 0x4B, Numpad5: 0x4C, Numpad6: 0x4D, NumpadAdd: 0x4E,
  Numpad1: 0x4F, Numpad2: 0x50, Numpad3: 0x51, Numpad0: 0x52, NumpadDecimal: 0x53,
  F11: 0x57, F12: 0x58, NumpadEnter: 0x1C,   // VCC's layouts have no keypad ENTER: make it ENTER
  ControlRight: 0x9D, NumpadDivide: 0xB5,
  AltRight: 0xB8, Home: 0xC7, ArrowUp: 0xC8, PageUp: 0xC9, ArrowLeft: 0xCB,
  ArrowRight: 0xCD, End: 0xCF, ArrowDown: 0xD0, PageDown: 0xD1, Insert: 0xD2, Delete: 0xD3,
};

// VCC's special keys (its About box lists them): F3/F4 overclock down/up,
// F5 soft reset (Shift+F5 hard), F6 RGB/composite, F7 pause, F8 throttle,
// F9 hard reset, F11 full screen.
const OVERCLOCKS = [0, 4, 8];
function specialKey(e) {
  const set = (key, v, after) => { settings[key] = v; saveSettings(); const sel = { overclock: 'selOc', rgb: 'selMon', throttle: 'selSpeed' }[key]; if (sel) $(sel).value = String(v); if (after) after(); };
  switch (e.code) {
    case 'F3': case 'F4': {
      const i = Math.max(0, OVERCLOCKS.indexOf(+settings.overclock)) + (e.code === 'F4' ? 1 : -1);
      set('overclock', OVERCLOCKS[Math.max(0, Math.min(OVERCLOCKS.length - 1, i))], applyConfig);
      toast(+settings.overclock ? `Double speed overclock: ${(+settings.overclock * 0.895).toFixed(2)} MHz` : 'Overclock off');
      return true;
    }
    case 'F5': if (e.shiftKey) hardReset(); else vcc.vcc_reset(0); return true;
    case 'F6': set('rgb', +settings.rgb ? 0 : 1, applyConfig); toast(+settings.rgb ? 'RGB monitor' : 'Composite monitor'); return true;
    case 'F7': $('btnPause').click(); return true;
    case 'F8': set('throttle', +settings.throttle ? 0 : 1); toast(+settings.throttle ? 'Normal speed' : 'Fast as possible'); return true;
    case 'F9': hardReset(); return true;
    case 'F11': if (document.fullscreenElement) document.exitFullscreen(); else document.documentElement.requestFullscreen().catch(() => {}); return true;
  }
  return false;
}

// Some Android keyboards and emulators (BlueStacks among them) leave
// KeyboardEvent.code empty or nonstandard. Fall back to the key's meaning.
const KEY_NAMES = { Enter: 'Enter', Backspace: 'Backspace', Escape: 'Escape', Tab: 'Tab', ' ': 'Space',
  ArrowUp: 'ArrowUp', ArrowDown: 'ArrowDown', ArrowLeft: 'ArrowLeft', ArrowRight: 'ArrowRight', Home: 'Home', Shift: 'ShiftLeft', Control: 'ControlLeft', Alt: 'AltLeft' };
function dikFor(e) {
  if (DIK[e.code] !== undefined) return DIK[e.code];
  if (e.keyCode === 13) return DIK.Enter;
  const k = e.key || '';
  if (KEY_NAMES[k]) return DIK[KEY_NAMES[k]];
  if (/^[a-z]$/i.test(k)) return DIK['Key' + k.toUpperCase()];
  if (/^[0-9]$/.test(k)) return DIK['Digit' + k];
  return undefined;
}

function physicalKey(e, down) {
  if (down) showKeyTest(e);
  if (!vcc || isDialogOpen()) return;
  if (keyJoystick(e, down)) return;
  if (/^F([3-9]|11)$/.test(e.code)) { e.preventDefault(); if (down && !e.repeat) specialKey(e); return; }
  const code = dikFor(e);
  if (code === undefined) return;
  e.preventDefault();
  if (e.repeat) return;
  startAudio();
  keyChange(code, down ? 1 : 0);
}

// Each key's presses and releases are spaced at least KEY_MIN_FRAMES apart.
// A key released within a frame or two of being pressed falls between two of
// the CoCo's keyboard scans and is never seen, and some keyboards and
// emulators send exactly that (BlueStacks's ENTER: down and up a millisecond
// apart). A change that comes too soon waits, and everything typed after it
// waits behind it, so keys reach the CoCo in the order they were typed.
const KEY_MIN_FRAMES = 3;   // 50 ms at normal speed
const keyLast = new Map(), keyQueue = [];
let emuFrames = 0;
const keyReady = (code) => emuFrames - (keyLast.get(code) ?? -KEY_MIN_FRAMES) >= KEY_MIN_FRAMES;
function keyChange(code, state) {
  keyQueue.push([code, state]);
  drainKeys();
}
function drainKeys() {
  while (keyQueue.length && keyReady(keyQueue[0][0])) {
    const [code, state] = keyQueue.shift();
    keyLast.set(code, emuFrames);
    vcc.vcc_key(code, state);
  }
}
// Called after every emulated frame.
function releaseHeldKeys() {
  emuFrames++;
  drainKeys();
}

// ☰ → Configuration → Keyboard shows what the last physical key sent, so a
// keyboard that misbehaves can be diagnosed.
function showKeyTest(e) {
  const el = $('keyTest');
  if (!el) return;
  const code = dikFor(e);
  el.textContent = `Last key: code "${e.code}", key "${e.key}", keyCode ${e.keyCode} → ${code === undefined ? 'not a CoCo key' : 'CoCo key'}`;
}
window.addEventListener('keydown', (e) => physicalKey(e, true));
// A button that keeps focus after a tap would be "clicked" again by a
// physical ENTER or SPACE (it was: ENTER restarted video recording). Drop
// focus after every tap so keys only ever go to the CoCo.
document.addEventListener('click', (e) => {
  const el = e.target.closest && e.target.closest('button, summary, a');
  if (el) el.blur();
});
window.addEventListener('keyup', (e) => physicalKey(e, false));

// ---------------------------------------------------------------- on-screen CoCo 3 keyboard

// [label, shifted label, column, row]. Matrix is PIA0: column = $FF02 bit, row = $FF00 bit.
const M = {
  '@': [0, 0], A: [1, 0], B: [2, 0], C: [3, 0], D: [4, 0], E: [5, 0], F: [6, 0], G: [7, 0],
  H: [0, 1], I: [1, 1], J: [2, 1], K: [3, 1], L: [4, 1], M: [5, 1], N: [6, 1], O: [7, 1],
  P: [0, 2], Q: [1, 2], R: [2, 2], S: [3, 2], T: [4, 2], U: [5, 2], V: [6, 2], W: [7, 2],
  X: [0, 3], Y: [1, 3], Z: [2, 3], UP: [3, 3], DOWN: [4, 3], LEFT: [5, 3], RIGHT: [6, 3], SPACE: [7, 3],
  0: [0, 4], 1: [1, 4], 2: [2, 4], 3: [3, 4], 4: [4, 4], 5: [5, 4], 6: [6, 4], 7: [7, 4],
  8: [0, 5], 9: [1, 5], ':': [2, 5], ';': [3, 5], ',': [4, 5], '-': [5, 5], '.': [6, 5], '/': [7, 5],
  ENTER: [0, 6], CLEAR: [1, 6], BREAK: [2, 6], ALT: [3, 6], CTRL: [4, 6], F1: [5, 6], F2: [6, 6], SHIFT: [7, 6],
};
// The CoCo 3 keyboard's arrangement, as XRoar draws it.
const LAYOUT = [
  [['1', '!'], ['2', '"'], ['3', '#'], ['4', '$'], ['5', '%'], ['6', '&'], ['7', "'"], ['8', '('], ['9', ')'], ['0', ''], [':', '*'], ['-', '='], ['GAP', '', 'gap'], ['BREAK', '', 'brk', 'BRK']],
  [['ALT', '', 'mod'], ['Q'], ['W'], ['E'], ['R'], ['T'], ['Y'], ['U'], ['I'], ['O'], ['P'], ['@'], ['CLEAR', '', 'mod', 'CLR'], ['UP', '', '', '↑']],
  [['CTRL', '', 'mod'], ['A'], ['S'], ['D'], ['F'], ['G'], ['H'], ['J'], ['K'], ['L'], [';', '+'], ['ENTER', '', 'wide'], ['LEFT', '', '', '←'], ['RIGHT', '', '', '→']],
  [['SHIFT', '', 'wide'], ['Z'], ['X'], ['C'], ['V'], ['B'], ['N'], ['M'], [',', '<'], ['.', '>'], ['/', '?'], ['SHIFT', '', 'wide'], ['DOWN', '', '', '↓']],
  [['SPACE', '', 'space', ' '], ['F1', '', 'mod'], ['F2', '', 'mod']],
];
const MODIFIERS = new Set(['SHIFT', 'CTRL', 'ALT']);
const latched = new Set();
const keyEls = {};

function buildKeyboard() {
  const kbd = $('kbd');
  for (const row of LAYOUT) {
    const r = document.createElement('div');
    r.className = 'krow';
    for (const [name, shifted = '', cls = '', label] of row) {
      const k = document.createElement('div');
      k.className = 'k' + (cls ? ' ' + cls : '');
      k.innerHTML = (shifted ? `<small>${shifted}</small>` : '') + (label || name);
      k.dataset.key = name;
      (keyEls[name] = keyEls[name] || []).push(k);
      r.appendChild(k);
    }
    kbd.appendChild(r);
  }
  // Multi-touch: each pointer presses the key it went down on.
  const pressed = new Map();
  kbd.addEventListener('pointerdown', (e) => {
    const k = e.target.closest('.k');
    if (!k) return;
    e.preventDefault();
    startAudio();
    kbd.setPointerCapture(e.pointerId);
    const name = k.dataset.key;
    pressed.set(e.pointerId, name);
    if (MODIFIERS.has(name) && +settings.sticky) {
      // Sticky modifiers latch: tap SHIFT, then a key. Tap again to release.
      if (latched.has(name)) { latched.delete(name); matrix(name, false); setLook(name, false); }
      else { latched.add(name); matrix(name, true); setLook(name, true); }
      return;
    }
    matrix(name, true);
    setLook(name, true);
    if (navigator.vibrate) navigator.vibrate(8);
  });
  const release = (e) => {
    const name = pressed.get(e.pointerId);
    if (!name) return;
    pressed.delete(e.pointerId);
    if (MODIFIERS.has(name) && +settings.sticky) return;
    // Hold the key long enough for the CoCo's keyboard scan to see it.
    setTimeout(() => {
      matrix(name, false);
      setLook(name, false);
      for (const m of latched) { matrix(m, false); setLook(m, false); }
      latched.clear();
    }, 60);
  };
  kbd.addEventListener('pointerup', release);
  kbd.addEventListener('pointercancel', release);
}

function matrix(name, down) {
  const pos = M[name];
  if (pos && vcc) vcc.vcc_matrix_key(pos[0], pos[1], down ? 1 : 0);
}
function setLook(name, down) {
  for (const el of keyEls[name] || []) el.classList.toggle(MODIFIERS.has(name) && +settings.sticky ? 'latched' : 'down', down);
}

// ---------------------------------------------------------------- joystick

const joy = { x: 32, y: 32, b: 0 };
// Port sides as the core numbers them: 0 right, 1 left.
function portsUsing(src) {
  const out = [];
  if (settings.joyPorts.right.src === src) out.push(0);
  if (settings.joyPorts.left.src === src) out.push(1);
  return out;
}
function sendJoy() { if (vcc) for (const side of portsUsing('stick')) vcc.vcc_joystick(side, joy.x, joy.y, joy.b); }
function joyPortLabel() {
  const s = portsUsing('stick');
  $('joyside').textContent = s.length === 2 ? 'Both ports' : s[0] === 0 ? 'Right port' : s[0] === 1 ? 'Left port' : 'Not connected';
}

// Keyboard joystick, as VCC's: arrows move it, F1 and F2 fire.
const keyJoy = { l: 0, r: 0, u: 0, d: 0, b: 0 };
const KEYJOY = { ArrowLeft: 'l', ArrowRight: 'r', ArrowUp: 'u', ArrowDown: 'd', F1: 1, F2: 2 };
function keyJoystick(e, down) {
  const sides = portsUsing('keys'), k = KEYJOY[e.code];
  if (!sides.length || k === undefined) return false;
  e.preventDefault();
  if (typeof k === 'number') keyJoy.b = down ? keyJoy.b | k : keyJoy.b & ~k; else keyJoy[k] = down ? 1 : 0;
  const x = keyJoy.l && !keyJoy.r ? 0 : keyJoy.r && !keyJoy.l ? 63 : 32;
  const y = keyJoy.u && !keyJoy.d ? 0 : keyJoy.d && !keyJoy.u ? 63 : 32;
  for (const side of sides) vcc.vcc_joystick(side, x, y, keyJoy.b);
  return true;
}

function buildJoystick() {
  const stick = $('stick'), knob = $('knob');
  let active = null;
  const move = (e) => {
    const r = stick.getBoundingClientRect();
    const cx = r.left + r.width / 2, cy = r.top + r.height / 2;
    let dx = (e.clientX - cx) / (r.width / 2), dy = (e.clientY - cy) / (r.height / 2);
    const len = Math.hypot(dx, dy);
    if (len > 1) { dx /= len; dy /= len; }
    knob.style.left = (r.width / 2 - 32 + dx * (r.width / 2 - 32)) + 'px';
    knob.style.top = (r.height / 2 - 32 + dy * (r.height / 2 - 32)) + 'px';
    joy.x = Math.round(31.5 + dx * 31.5);
    joy.y = Math.round(31.5 + dy * 31.5);
    sendJoy();
  };
  stick.addEventListener('pointerdown', (e) => { active = e.pointerId; stick.setPointerCapture(e.pointerId); startAudio(); move(e); });
  stick.addEventListener('pointermove', (e) => { if (e.pointerId === active) move(e); });
  const center = (e) => {
    if (e.pointerId !== active) return;
    active = null;
    if (!+settings.joyCenter) return;   // non-centering: the stick stays where it was let go
    knob.style.left = '53px'; knob.style.top = '53px';
    joy.x = 32; joy.y = 32; sendJoy();
  };
  stick.addEventListener('pointerup', center);
  stick.addEventListener('pointercancel', center);
  for (const [id, bit] of [['fire1', 1], ['fire2', 2]]) {
    const el = $(id);
    el.addEventListener('pointerdown', (e) => { e.preventDefault(); el.setPointerCapture(e.pointerId); joy.b |= bit; el.classList.add('down'); sendJoy(); });
    const up = () => { joy.b &= ~bit; el.classList.remove('down'); sendJoy(); };
    el.addEventListener('pointerup', up);
    el.addEventListener('pointercancel', up);
  }
  // Tapping the port label opens the joystick settings.
  $('joyside').addEventListener('click', () => openMenu('joystick'));
  joyPortLabel();

  // Touching the screen itself can drive the right joystick, like VCC's mouse.
  const scr = $('screenWrap');
  const touchMove = (e) => {
    const sides = portsUsing('touch');
    if (!sides.length) return;
    const r = canvas.getBoundingClientRect();
    const scale = Math.min(r.width / 640, r.height / 480);
    const w = 640 * scale, h = 480 * scale;
    const left = r.left + (r.width - w) / 2, top = r.top + (r.height - h) / 2;
    const fx = (e.clientX - left) / w, fy = (e.clientY - top) / h;
    for (const side of sides) vcc.vcc_joystick_raw(side, Math.round(Math.max(0, Math.min(1, fx)) * 16383), Math.round(Math.max(0, Math.min(1, fy)) * 16383), e.buttons ? 1 : 0);
  };
  scr.addEventListener('pointerdown', (e) => { startAudio(); scr.setPointerCapture(e.pointerId); touchMove(e); });
  scr.addEventListener('pointermove', touchMove);
  scr.addEventListener('pointerup', (e) => touchMove({ clientX: e.clientX, clientY: e.clientY, buttons: 0 }));
}

let padWasActive = false;
function pollGamepad() {
  if (!navigator.getGamepads) return;
  const pads = navigator.getGamepads();
  for (const p of pads) {
    if (!p) continue;
    const ax = p.axes[0] || 0, ay = p.axes[1] || 0;
    const b = (p.buttons[0] && p.buttons[0].pressed ? 1 : 0) | (p.buttons[1] && p.buttons[1].pressed ? 2 : 0);
    const active = Math.abs(ax) > 0.05 || Math.abs(ay) > 0.05 || b;
    if (active || padWasActive) {
      for (const side of portsUsing('stick')) vcc.vcc_joystick(side, Math.round(31.5 + ax * 31.5), Math.round(31.5 + ay * 31.5), b);
      padWasActive = active;
    }
    return;
  }
}

// ---------------------------------------------------------------- files

function pickFile(accept) {
  return new Promise((resolve) => {
    const input = $('filePick');
    input.value = '';
    input.accept = accept || '';
    input.onchange = async () => {
      const f = input.files && input.files[0];
      if (!f) return resolve(null);
      resolve({ name: f.name, bytes: new Uint8Array(await f.arrayBuffer()) });
    };
    input.click();
  });
}

function pickFiles() {
  return new Promise((resolve) => {
    const input = $('filePick');
    input.value = '';
    input.multiple = true;
    input.onchange = async () => {
      input.multiple = false;
      const out = [];
      for (const f of Array.from(input.files || [])) out.push({ name: f.name, bytes: new Uint8Array(await f.arrayBuffer()) });
      resolve(out);
    };
    input.click();
  });
}

function saveToDevice(name, bytes) {
  if (window.AndroidHost && window.AndroidHost.saveBegin) {
    // Sent in pieces: a hard drive image is too big for one bridge call.
    window.AndroidHost.saveBegin(name);
    for (let i = 0; i < bytes.length; i += 0x100000) {
      const part = bytes.subarray(i, i + 0x100000);
      let bin = '';
      for (let j = 0; j < part.length; j += 0x8000) bin += String.fromCharCode.apply(null, part.subarray(j, j + 0x8000));
      window.AndroidHost.saveChunk(btoa(bin));
    }
    window.AndroidHost.saveEnd();
    return;
  }
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([bytes]));
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 5000);
}

async function loadRomFile(kind) {
  const f = await pickFile('');
  if (!f) return false;
  f.bytes = stripLoadHeader(f.bytes).slice();
  if (kind === 'coco3' && f.bytes.length < 32768) { toast(`That coco3.rom is ${f.bytes.length} bytes; it must be 32768. The file is incomplete.`, 5000); return false; }
  if (kind === 'disk11' && f.bytes.length > 16384) { toast('That file is too big to be a disk ROM'); return false; }
  if (kind === 'ssc-pic' && f.bytes.length < 4096) { toast('The PIC7040 ROM is 4096 bytes'); return false; }
  if (kind === 'ssc-spo' && f.bytes.length < 2048) { toast('The SP0256-AL2 ROM is 2048 bytes'); return false; }
  await Store.put('rom:' + kind, f.bytes.buffer);
  toast(`${f.name} loaded`);
  updateRomLabels();
  return true;
}

async function updateRomLabels() {
  const rom = await Store.get('rom:coco3');
  const disk = await Store.get('rom:disk11');
  const set = (id, name, v) => { const el = $(id); el.textContent = `${name}: ${v ? 'loaded' : 'missing'}`; el.classList.toggle('ok', !!v); };
  set('romCoco3', 'coco3.rom', rom);
  set('romDisk', 'disk11.rom', disk);
  set('romOrch', 'orch90.rom', await Store.get('rom:orch90'));
  set('romPic', 'pic-7040-510.bin', await Store.get('rom:ssc-pic'));
  set('romSpo', 'sp0256-al2.bin', await Store.get('rom:ssc-spo'));
  set('romPsg', 'psg_firmware_v1.bin', await Store.get('rom:psg'));
  set('romHdb', 'HDB-DOS ROM', await Store.get('rom:hdbdos'));
  $('wStatus').textContent = `coco3.rom ${rom ? '✓' : '—'}   disk11.rom ${disk ? '✓' : '—'}`;
}

function renderSlots() {
  const box = $('slots');
  box.innerHTML = '';
  for (let i = 0; i < 4; i++) {
    const slot = settings.slots[i];
    const row = document.createElement('div');
    row.className = 'slot';
    const sel = document.createElement('select');
    CART_TYPES.forEach((t, n) => { const o = document.createElement('option'); o.value = n; o.textContent = t; sel.appendChild(o); });
    sel.value = slot ? slot.type : 0;
    row.innerHTML = `<b>${i + 1}</b>`;
    row.appendChild(sel);
    const info = document.createElement('span');
    info.className = 'name';
    if (slot && NEEDS_FILE[slot.type]) info.textContent = slot.name || 'no ROM';
    sel.onchange = async () => {
      const type = +sel.value;
      if (type === 1 && settings.slots.some((x, n) => n !== i && x && +x.type === 1)) {
        toast('Only one FD-502 can be plugged in'); sel.value = slot ? slot.type : 0; return;
      }
      if (slot && slot.key) await Store.del(slot.key);
      if (NEEDS_FILE[type]) {
        const f = await pickFile('');
        if (!f) { sel.value = slot ? slot.type : 0; return; }
        f.bytes = stripLoadHeader(f.bytes).slice();
        const key = `cart:${i}:${f.name}`;
        await Store.put(key, f.bytes.buffer);
        settings.slots[i] = { type, key, name: f.name };
      } else {
        settings.slots[i] = type ? { type } : null;
      }
      saveSettings();
      renderSlots();
      await hardReset();
      toast(`Slot ${i + 1}: ${CART_TYPES[type]}`);
    };
    row.appendChild(info);
    box.appendChild(row);
  }
}

function renderDrives() {
  const box = $('drives');
  box.innerHTML = '';
  for (let d = 0; d < 4; d++) {
    const info = settings.drives[d];
    const row = document.createElement('div');
    row.className = 'drive';
    row.innerHTML = `<b>${d}</b><span class="name">${info ? info.name : 'empty'}</span>`;
    const ins = document.createElement('button');
    ins.textContent = 'Insert';
    ins.onclick = () => insertDisk(d);
    row.appendChild(ins);
    if (info) {
      const sv = document.createElement('button');
      sv.textContent = 'Save';
      sv.onclick = () => exportDisk(d);
      const ej = document.createElement('button');
      ej.textContent = 'Eject';
      ej.onclick = () => ejectDisk(d);
      row.appendChild(sv);
      row.appendChild(ej);
    }
    box.appendChild(row);
  }
}

async function insertDisk(d, preset) {
  const f = preset || await pickFile('');
  if (!f) return;
  if (settings.drives[d]) await ejectDisk(d, true);
  const key = `disk:${Date.now()}:${f.name}`;
  await Store.put(key, f.bytes.buffer);
  settings.drives[d] = { key, name: f.name };
  saveSettings();
  mountBytes(d, settings.drives[d], f.bytes);
  renderDrives();
  if (!haveDiskRom) toast('Load disk11.rom (System ROMs) to use the drives', 3500);
  else toast(`${f.name} in drive ${d}`);
}

async function ejectDisk(d, quiet) {
  await saveDirtyDisks();
  const info = settings.drives[d];
  if (info && info.modified && confirm(`${info.name} was changed. Save a copy to the device before ejecting?`)) {
    await exportDisk(d);
  }
  vcc.vcc_unmount_disk(d);
  Files.delete('drive' + d);
  if (info) await Store.del(info.key);
  settings.drives[d] = null;
  saveSettings();
  renderDrives();
  if (!quiet && info) toast(`Ejected ${info.name}`);
}

async function exportDisk(d) {
  await saveDirtyDisks();
  const info = settings.drives[d];
  if (!info) return;
  const data = await Store.get(info.key);
  if (data) saveToDevice(info.name, new Uint8Array(data));
}

// ---------------------------------------------------------------- UI wiring

function isDialogOpen() {
  return document.querySelector('.dialog.open') || $('menu').classList.contains('open');
}
function showWelcome() { updateRomLabels(); $('welcome').classList.add('open'); }

function bindSelect(id, key, after) {
  const el = $(id);
  el.value = String(settings[key]);
  el.onchange = () => { settings[key] = +el.value; saveSettings(); if (after) after(); };
}

function setPanel(which) {
  settings.panel = which;
  saveSettings();
  $('tabKbd').classList.toggle('on', which === 'kbd');
  $('tabJoy').classList.toggle('on', which === 'joy');
  $('tabCmd').classList.toggle('on', which === 'cmd');
  $('kbd').style.display = which === 'kbd' ? '' : 'none';
  $('joy').classList.toggle('on', which === 'joy');
  $('cmds').classList.toggle('on', which === 'cmd');
  applyLayout();
}

// Joystick mode in landscape puts the stick on one side of the screen and
// the fire buttons on the other; joySide swaps them (in portrait too).
function applyLayout() {
  const landscape = window.matchMedia('(orientation: landscape)').matches;
  document.body.classList.toggle('land-joy', landscape && settings.panel === 'joy' && settings.showControls);
  document.body.classList.toggle('joy-swap', settings.joySide === 'right');
}

function wireUi() {
  buildKeyboard();
  buildJoystick();
  setPanel(settings.panel);
  $('controls').classList.toggle('hidden', !settings.showControls);
  $('tabKbd').onclick = () => setPanel('kbd');
  $('tabJoy').onclick = () => setPanel('joy');
  $('tabCmd').onclick = () => setPanel('cmd');
  window.matchMedia('(orientation: landscape)').addEventListener('change', applyLayout);
  buildCommands();
  bindSelect('selJoyCenter', 'joyCenter');
  $('selJoySide').value = settings.joySide;
  $('selJoySide').onchange = () => { settings.joySide = $('selJoySide').value; saveSettings(); applyLayout(); };
  $('selPicture').value = settings.picture;
  $('selPicture').onchange = () => { settings.picture = $('selPicture').value; saveSettings(); applyPicture(); };
  applyPicture();
  for (const id of ['btnLoad', 'cmdLoad']) $(id).onclick = () => { $('menu').classList.remove('open'); loadProgram(false); };
  for (const id of ['btnRun', 'cmdRun']) $(id).onclick = () => { $('menu').classList.remove('open'); loadProgram(true); };
  for (const id of ['btnSnap', 'cmdSnap']) $(id).onclick = () => quickSnapshot();
  for (const id of ['btnRestore', 'cmdRestore']) $(id).onclick = () => quickRestore();
  $('btnSnapSave').onclick = () => saveSnapshotFile();
  $('tapeInsert').onclick = () => insertTape();
  $('tapeBlank').onclick = async () => { await newBlankTape(); toast('Blank tape in the deck: press Record, then CSAVE'); };
  $('tapeSave').onclick = () => exportTape();
  $('tapeEject').onclick = () => ejectTape();
  $('tapePlay').onclick = () => { startAudio(); tapeControl(1); };
  $('tapeRec').onclick = () => tapeControl(2);
  $('tapeStop').onclick = () => tapeControl(0);
  $('tapeRew').onclick = () => tapeRewind();
  $('tapeFwd').onclick = () => tapeForward();
  bindSelect('selTapeFast', 'tapeFast', applyConfig);
  bindSelect('selGime', 'gime86', applyConfig);
  $('btnSnapLoad').onclick = () => loadSnapshotFile();
  $('btnShot').onclick = () => screenshot();
  $('btnRec').onclick = () => toggleRecording();
  $('btnShotMenu').onclick = () => { $('menu').classList.remove('open'); setTimeout(screenshot, 100); };
  $('btnRecMenu').onclick = () => { $('menu').classList.remove('open'); toggleRecording(); };
  $('btnHelp').onclick = () => openHelp();
  $('btnMenuHelp').onclick = () => { $('menu').classList.remove('open'); openHelp(); };
  $('helpClose').onclick = () => $('helpDlg').classList.remove('open');
  $('helpDlg').addEventListener('click', (e) => { if (e.target.id === 'helpDlg') $('helpDlg').classList.remove('open'); });
  wireAccordions();
  new ResizeObserver(() => applyScanlines()).observe($('screenWrap'));
  $('aboutVersion').textContent = `Version ${VERSION} · based on VCC ${VCC_VERSION}`;
  for (const el of document.querySelectorAll('a.bugLink')) el.href = bugReportUrl();
  if (navigator.storage && navigator.storage.persist) navigator.storage.persist().catch(() => {});
  $('btnKbd').onclick = () => {
    settings.showControls = !settings.showControls;
    saveSettings();
    $('controls').classList.toggle('hidden', !settings.showControls);
    applyLayout();
  };
  $('btnMenu').onclick = () => { renderPrinter(); renderDrives(); renderTape(); renderSlots(); renderHdds(); updateRomLabels(); $('menu').classList.add('open'); };
  $('btnClose').onclick = () => $('menu').classList.remove('open');
  $('menu').addEventListener('click', (e) => { if (e.target.id === 'menu') $('menu').classList.remove('open'); });

  $('btnHard').onclick = () => { hardReset(); $('menu').classList.remove('open'); };
  $('btnSoft').onclick = () => { vcc.vcc_reset(0); $('menu').classList.remove('open'); };
  $('btnPause').onclick = () => { paused = !paused; $('btnPause').textContent = paused ? 'Resume' : 'Pause'; };

  bindSelect('selCpu', 'cpu', () => { hardReset(); toast('CPU changed; machine reset'); });
  bindSelect('selRam', 'ram', () => { hardReset(); toast('Memory changed; machine reset'); });
  bindSelect('selMon', 'rgb', applyConfig);
  bindSelect('selScan', 'scan', applyScanlines);
  bindSelect('selSpeed', 'throttle');
  bindSelect('selOc', 'overclock', applyConfig);
  $('chkTurbo').checked = !!settings.turboDisk;
  $('chkTurbo').onchange = () => { settings.turboDisk = $('chkTurbo').checked ? 1 : 0; saveSettings(); applyConfig(); };

  $('btnBlank').onclick = async () => {
    const d = settings.drives.findIndex((x) => !x);
    if (d < 0) { toast('Eject a disk first'); return; }
    // An all-$FF 35-track image is an empty, already-formatted RS-DOS disk.
    const bytes = new Uint8Array(35 * 18 * 256).fill(0xff);
    await insertDisk(d, { name: `blank${d}.dsk`, bytes });
  };

  bindSelect('selMpi', 'mpi', hardReset);
  bindSelect('selSwitch', 'switchSlot', hardReset);
  bindSelect('selSticky', 'sticky');
  const SRC = [['none', 'Not connected'], ['stick', 'On-screen stick'], ['touch', 'Touch on screen'], ['keys', 'Keyboard']];
  const EMU = [[0, 'Standard'], [2, 'Tandy Hi-Res'], [3, 'CC-MAX (CoCo Max)']];
  for (const [side, cap] of [['left', 'Left'], ['right', 'Right']]) {
    const src = $(`selJoy${cap}Src`), emu = $(`selJoy${cap}Emu`);
    src.innerHTML = SRC.map(([v, t]) => `<option value="${v}">${t}</option>`).join('');
    emu.innerHTML = EMU.map(([v, t]) => `<option value="${v}">${t}</option>`).join('');
    src.value = settings.joyPorts[side].src;
    emu.value = String(settings.joyPorts[side].emu);
    src.onchange = () => { settings.joyPorts[side].src = src.value; saveSettings(); joyPortLabel(); vcc.vcc_joystick(side === 'right' ? 0 : 1, 32, 32, 0); };   // re-center a port whose input changed
    emu.onchange = () => { settings.joyPorts[side].emu = +emu.value; saveSettings(); applyConfig(); };
  }
  // BitBanger printer capture
  $('chkPrnLF').checked = !!+settings.prnLF;
  $('chkPrnLF').onchange = () => { settings.prnLF = $('chkPrnLF').checked ? 1 : 0; saveSettings(); if (printing) vcc.vcc_printer(1, settings.prnLF); };
  $('prnStart').onclick = () => printerStart();
  $('prnStop').onclick = () => printerStop();
  $('prnShow').onclick = () => printerShow();
  $('prnSave').onclick = () => printerSave();
  $('selDiskRom').value = settings.diskRom;
  $('selDiskRom').onchange = async () => { settings.diskRom = $('selDiskRom').value; saveSettings(); await hardReset(); toast('Disk controller ROM changed; machine reset'); };
  $('btnHdbRom').onclick = async () => { if (await loadRomFile('hdbdos')) await hardReset(); };
  $('btnOrchRom').onclick = async () => { if (await loadRomFile('orch90')) await hardReset(); };
  $('btnPicRom').onclick = async () => { if (await loadRomFile('ssc-pic')) await hardReset(); };
  $('btnSpoRom').onclick = async () => { if (await loadRomFile('ssc-spo')) await hardReset(); };
  $('btnPsgRom').onclick = async () => { if (await loadRomFile('psg')) await hardReset(); };

  $('btnPaste').onclick = () => { $('menu').classList.remove('open'); $('pasteDlg').classList.add('open'); };
  $('pasteCancel').onclick = () => $('pasteDlg').classList.remove('open');
  $('pasteGo').onclick = () => {
    const text = $('pasteText').value.replace(/\r\n?/g, '\n').replace(/\n/g, '\r');
    $('pasteDlg').classList.remove('open');
    if (text) withStr(text, (p) => vcc.vcc_paste(p));
  };
  $('btnCopy').onclick = () => {
    const text = cstr(vcc.vcc_copy_screen_text());
    $('menu').classList.remove('open');
    $('screenText').value = text;
    $('textDlg').querySelector('h1').textContent = 'Screen text';
    $('textDlg').classList.add('open');
    if (navigator.clipboard) navigator.clipboard.writeText(text).catch(() => {});
  };
  $('textClose').onclick = () => $('textDlg').classList.remove('open');

  $('btnRom').onclick = async () => { if (await loadRomFile('coco3')) { await hardReset(); } };
  $('btnDiskRom').onclick = async () => { if (await loadRomFile('disk11')) { await hardReset(); } };
  $('wRom').onclick = async () => {
    if (await loadRomFile('coco3')) {
      $('welcome').classList.remove('open');
      await hardReset();
    }
  };
  $('wDisk').onclick = () => loadRomFile('disk11');

  document.addEventListener('visibilitychange', () => {
    if (document.hidden) { saveDirtyDisks(); if (audioCtx) audioCtx.suspend(); }
    else if (audioCtx) audioCtx.resume();
  });
  setInterval(saveDirtyDisks, 3000);
  window.addEventListener('pointerdown', startAudio, { once: true });
}

// ---------------------------------------------------------------- Load / Run (as XRoar)

function decbSegments(bytes) {
  const segs = [];
  let i = 0, exec = null;
  while (i + 5 <= bytes.length) {
    const type = bytes[i], len = (bytes[i + 1] << 8) | bytes[i + 2], addr = (bytes[i + 3] << 8) | bytes[i + 4];
    if (type === 0x00) { segs.push({ addr, data: bytes.subarray(i + 5, i + 5 + len) }); i += 5 + len; }
    else if (type === 0xff) { exec = addr; break; }
    else return null;
  }
  return segs.length ? { segs, exec } : null;
}

// Reads an RS-DOS directory (track 17, sectors 3-11) from a .dsk.
function dskDirectory(bytes) {
  const off = bytes.length % 256;
  const files = [];
  for (let sec = 3; sec <= 11; sec++) {
    const base = off + (17 * 18 + sec - 1) * 256;
    for (let e = 0; e < 256; e += 32) {
      const first = bytes[base + e];
      if (first === 0xff || first === undefined) return files;
      if (first === 0) continue;
      const name = String.fromCharCode(...bytes.subarray(base + e, base + e + 8)).trim();
      const ext = String.fromCharCode(...bytes.subarray(base + e + 8, base + e + 11)).trim();
      files.push({ name, ext, type: bytes[base + e + 11] });
    }
  }
  return files;
}

function typeText(text) { withStr(text, (p) => vcc.vcc_paste(p)); }

async function loadProgram(run) {
  const f = await pickFile('');
  if (!f) return;
  const ext = (f.name.split('.').pop() || '').toLowerCase();
  startAudio();
  if (ext === 'cas' || ext === 'wav') { await loadTapeProgram(f, run); return; }
  if (ext === 'dsk') {
    await insertDisk(0, f);
    if (!run) return;
    const files = dskDirectory(f.bytes);
    const ml = files.filter((x) => x.type === 2), bas = files.filter((x) => x.type === 0);
    if (ml.length === 1 && !bas.length) typeText(`LOADM"${ml[0].name}":EXEC\r`);
    else if (bas.length) typeText(`RUN"${bas[0].name}"\r`);
    else if (ml.length) typeText(`LOADM"${ml[0].name}":EXEC\r`);
    else typeText('DOS\r');   // no files: try booting it (OS-9)
    return;
  }
  if (ext === 'rom' || ext === 'ccc') {
    const bytes = stripLoadHeader(f.bytes).slice();
    const key = 'cart:0:' + f.name;
    await Store.put(key, bytes.buffer);
    settings.slots[0] = { type: 2, key, name: f.name };
    settings.switchSlot = 0;
    saveSettings();
    $('selSwitch').value = '0';
    await hardReset();
    toast(`${f.name} in slot 1, Multi-Pak switch on slot 1. Set it back to slot 4 for Disk BASIC.`, 5000);
    return;
  }
  const bin = decbSegments(f.bytes);
  if (!bin) { toast(`Can't load ${f.name}: Load takes .bin, .dsk, .rom, .cas and .wav files`, 4500); return; }
  for (const seg of bin.segs) for (let k = 0; k < seg.data.length; k++) vcc.vcc_poke(seg.addr + k, seg.data[k]);
  if (bin.exec !== null) {
    vcc.vcc_poke(0x9d, bin.exec >> 8);   // BASIC's EXEC address, so EXEC runs it later
    vcc.vcc_poke(0x9e, bin.exec & 0xff);
  }
  if (run && bin.exec !== null) {
    vcc.vcc_exec(bin.exec);
    toast(`Running ${f.name} at $${bin.exec.toString(16).toUpperCase()}`);
  } else {
    toast(`${f.name} loaded${bin.exec !== null ? `; EXEC runs it ($${bin.exec.toString(16).toUpperCase()})` : ''}`, 3500);
  }
}

// ---------------------------------------------------------------- cassette

// One tape deck, as on the real machine. The tape is the file "tape": a .cas
// byte stream, or a .wav converted on the way in to what the emulator plays,
// 8-bit unsigned mono at 44.1 kHz. TAPE_* match Cassette.h.
const TAPE_WAV = 1, TAPE_CAS = 2;
const TAPE_RATE = 44100;
const TAPE_MODES = ['Stopped', 'Playing', 'Recording', 'No tape'];
let tapeAutoRun = null;   // {seen, off, wait}: types RUN once CLOAD has finished

function tapeInfo() {
  const a = new Uint32Array(mem.buffer, vcc.vcc_tape_info(), 5);
  return { mode: a[0], motor: a[1], pos: a[2], size: a[3], kind: a[4] };
}

// Any PCM or float WAV -> 8-bit unsigned mono at 44.1 kHz.
function wavToTape(bytes) {
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const tag = (o) => String.fromCharCode(bytes[o], bytes[o + 1], bytes[o + 2], bytes[o + 3]);
  if (bytes.length < 12 || tag(0) !== 'RIFF' || tag(8) !== 'WAVE') return null;
  let fmt = null, data = null;
  for (let o = 12; o + 8 <= bytes.length;) {
    const id = tag(o), len = dv.getUint32(o + 4, true);
    if (id === 'fmt ') {
      fmt = { type: dv.getUint16(o + 8, true), ch: dv.getUint16(o + 10, true), rate: dv.getUint32(o + 12, true), bits: dv.getUint16(o + 22, true) };
      if (fmt.type === 0xfffe && len >= 26) fmt.type = dv.getUint16(o + 32, true);   // WAVE_FORMAT_EXTENSIBLE: the sub-format
    }
    else if (id === 'data') { data = { off: o + 8, len: Math.min(len, bytes.length - o - 8) }; break; }
    o += 8 + len + (len & 1);
  }
  if (!fmt || !data || !fmt.ch || !fmt.rate) return null;
  const width = fmt.bits >> 3, frame = width * fmt.ch;
  if (!width || (fmt.type !== 1 && !(fmt.type === 3 && fmt.bits === 32))) return null;
  const frames = Math.floor(data.len / frame);
  const sample = (i) => {   // -1..1, channels averaged
    let sum = 0;
    for (let c = 0; c < fmt.ch; c++) {
      const p = data.off + i * frame + c * width;
      if (fmt.type === 3) sum += dv.getFloat32(p, true);
      else if (width === 1) sum += (bytes[p] - 128) / 128;
      else if (width === 2) sum += dv.getInt16(p, true) / 32768;
      else if (width === 3) sum += ((bytes[p + 2] << 24 | bytes[p + 1] << 16 | bytes[p] << 8) >> 8) / 8388608;
      else sum += dv.getInt32(p, true) / 2147483648;
    }
    return sum / fmt.ch;
  };
  const outLen = Math.floor(frames * TAPE_RATE / fmt.rate);
  const out = new Uint8Array(outLen);
  const step = fmt.rate / TAPE_RATE;
  for (let i = 0; i < outLen; i++) {
    const x = i * step, k = Math.floor(x), t = x - k;
    const s = k + 1 < frames ? sample(k) * (1 - t) + sample(k + 1) * t : sample(k);
    out[i] = Math.max(0, Math.min(255, Math.round(128 + s * 127)));
  }
  return out;
}

function tapeToWav(raw) {
  const out = new Uint8Array(44 + raw.length);
  const dv = new DataView(out.buffer);
  out.set(enc.encode('RIFF'), 0); dv.setUint32(4, 36 + raw.length, true);
  out.set(enc.encode('WAVEfmt '), 8); dv.setUint32(16, 16, true);
  dv.setUint16(20, 1, true); dv.setUint16(22, 1, true);
  dv.setUint32(24, TAPE_RATE, true); dv.setUint32(28, TAPE_RATE, true);
  dv.setUint16(32, 1, true); dv.setUint16(34, 8, true);
  out.set(enc.encode('data'), 36); dv.setUint32(40, raw.length, true);
  out.set(raw, 44);
  return out;
}

// The first file on a tape: {name, type} (type 0 BASIC, 1 data, 2 machine
// code), or null. Works on bits, since a .cas need not be byte aligned and a
// .wav certainly is not. Decodes a .wav the way Cassette.cpp's WavtoCas does:
// a cycle of 10-30 samples is a 1, 31-50 a 0.
function tapeFirstFile(kind, data) {
  const bits = [];
  if (kind === TAPE_CAS) {
    for (let i = 0; i < Math.min(data.length, 4096); i++) for (let b = 0; b < 8; b++) bits.push((data[i] >> b) & 1);
  } else {
    let last = 0, lastRise = 0;
    for (let i = 0; i < Math.min(data.length, TAPE_RATE * 60) && bits.length < 32768; i++) {
      const s = data[i];
      if (last <= 0x80 && s > 0x80) {
        const w = i - lastRise;
        if (w >= 10 && w <= 50) bits.push(w > 30 ? 0 : 1);
        lastRise = i;
      }
      last = s;
    }
  }
  const byteAt = (p) => { let v = 0; for (let b = 0; b < 8; b++) v |= bits[p + b] << b; return v; };
  let reg = 0;
  for (let p = 0; p < bits.length; p++) {
    reg = (reg >>> 1) | (bits[p] << 15);
    if (p < 15 || (reg >> 8) !== 0x3c || (reg & 0xff) !== 0x55) continue;
    const at = p + 1;
    if (at + 16 > bits.length) return null;
    const type = byteAt(at), len = byteAt(at + 8);
    if (type !== 0 || len < 10 || at + 16 + len * 8 > bits.length) continue;
    let name = '';
    for (let k = 0; k < 8; k++) name += String.fromCharCode(byteAt(at + 16 + k * 8));
    return { name: name.trim(), type: byteAt(at + 16 + 64) };
  }
  return null;
}

async function mountTape() {
  const t = settings.tape;
  if (!t) return;
  const data = await Store.get(t.key);
  if (!data) { settings.tape = null; saveSettings(); return; }
  putFile('tape', new Uint8Array(data));
  vcc.vcc_tape_insert(t.kind);
}

async function insertTape(preset, quiet) {
  const f = preset || await pickFile('');
  if (!f) return null;
  const ext = (f.name.split('.').pop() || '').toLowerCase();
  let kind = TAPE_CAS, bytes = f.bytes;
  if (ext === 'wav') {
    bytes = wavToTape(f.bytes);
    if (!bytes) { toast(`Can't read ${f.name}: use a PCM or float WAV`, 4000); return null; }
    kind = TAPE_WAV;
  } else if (ext !== 'cas') { toast('Tapes are .cas or .wav files', 3500); return null; }
  await removeTape();
  const key = `tape:${Date.now()}`;
  await Store.put(key, bytes.slice().buffer);
  settings.tape = { key, name: f.name, kind };
  saveSettings();
  putFile('tape', bytes.slice());
  vcc.vcc_tape_insert(kind);
  renderTape();
  if (!quiet) toast(`${f.name} in the tape deck. Press Play, then type CLOAD or CLOADM.`, 4000);
  return { kind, bytes };
}

async function newBlankTape() {
  await removeTape();
  const stamp = new Date().toISOString().slice(0, 16).replace(/[:T]/g, '-');
  const key = `tape:${Date.now()}`;
  await Store.put(key, new ArrayBuffer(0));
  settings.tape = { key, name: `tape-${stamp}.cas`, kind: TAPE_CAS };
  saveSettings();
  putFile('tape', new Uint8Array(0));
  vcc.vcc_tape_insert(TAPE_CAS);
  renderTape();
}

async function saveTape() {
  if (!settings.tape) return;
  vcc.vcc_tape_sync();
  const f = Files.get('tape');
  if (f && f.dirty) {
    f.dirty = false;
    f.chunks.clear();
    await Store.put(settings.tape.key, f.data.slice().buffer);
  }
}

async function removeTape() {
  await saveTape();
  vcc.vcc_tape_eject();
  if (settings.tape) await Store.del(settings.tape.key);
  Files.delete('tape');
  settings.tape = null;
  tapeAutoRun = null;
  saveSettings();
}

async function ejectTape() {
  const t = settings.tape;
  if (!t) return;
  await saveTape();
  const info = tapeInfo();
  if (t.kind === TAPE_CAS && info.size && confirm(`Save ${t.name} to the device before ejecting?`)) await exportTape();
  await removeTape();
  renderTape();
  toast(`Ejected ${t.name}`);
}

async function exportTape() {
  const t = settings.tape;
  if (!t) return;
  await saveTape();
  const f = Files.get('tape');
  if (!f || !f.data.length) { toast('The tape is empty'); return; }
  if (t.kind === TAPE_WAV) saveToDevice(t.name.replace(/\.[^.]*$/, '') + '.wav', tapeToWav(f.data));
  else saveToDevice(t.name, f.data.slice());
}

async function tapeControl(mode) {
  if (mode === 2 && !settings.tape) await newBlankTape();
  if (mode !== 0 && !settings.tape) { toast('Insert a tape first'); return; }
  vcc.vcc_tape_mode(mode);
  if (mode === 0) await saveTape();
  renderTape();
  if (mode === 2) toast('Recording: type CSAVE"NAME" (or CSAVEM) to save onto the tape', 4000);
}

function tapeRewind() {
  vcc.vcc_tape_seek(0);
  renderTape();
}

// Tape time in seconds: exact for a .wav; for a .cas, VCC's waveforms
// average about 30.5 samples a bit, so about 5.5 ms a byte.
function tapeSeconds(info, bytes) { return info.kind === TAPE_WAV ? bytes / TAPE_RATE : bytes * 8 * 30.5 / TAPE_RATE; }
function fmtTime(sec) { sec = Math.round(sec); return `${Math.floor(sec / 60)}:${String(sec % 60).padStart(2, '0')}`; }

function renderTape() {
  if (!vcc) return;
  const t = settings.tape, info = tapeInfo();
  $('tapeName').textContent = t ? t.name : 'no tape';
  let pos = '';
  if (t) pos = `${TAPE_MODES[info.mode]}${info.motor ? ', motor on' : ''} · ${fmtTime(tapeSeconds(info, info.pos))} of ${fmtTime(tapeSeconds(info, info.size))}`;
  $('tapePos').textContent = pos;
  $('tapeMode').textContent = t ? ['STOP', 'PLAY', 'REC', '—'][info.mode] : '—';
  $('tapeCounter').textContent = String(Math.floor(tapeSeconds(info, info.pos)) % 1000).padStart(3, '0');
  // REC on a real deck holds PLAY down with it.
  $('tapePlay').classList.toggle('on', info.mode === 1 || info.mode === 2);
  $('tapeRec').classList.toggle('on', info.mode === 2);
  document.querySelector('.deck').classList.toggle('moving', !!(info.motor && (info.mode === 1 || info.mode === 2)));
}

// Where the next program on the tape starts, from byte or sample `from`: the
// leader in front of the next name block (block type 0). FWD winds to it.
function nextFileOffset(kind, data, from) {
  if (kind === TAPE_CAS) {
    for (let i = from + 1; i + 1 < data.length; i++) {
      if (data[i] !== 0x3c || data[i - 1] !== 0x55 || data[i + 1] !== 0x00) continue;
      let start = i - 1;
      while (start > 0 && data[start - 1] === 0x55) start--;
      if (start > from) return start;
    }
    return data.length;
  }
  let last = 0, lastRise = from, reg = 0, typeBits = -1, type = 0, syncAt = 0;
  for (let i = from; i < data.length; i++) {
    const v = data[i];
    if (last <= 0x80 && v > 0x80) {
      const w = i - lastRise;
      lastRise = i;
      if (w >= 10 && w <= 50) {
        const bit = w > 30 ? 0 : 1;
        if (typeBits >= 0) {
          type |= bit << typeBits;
          if (++typeBits === 8) {
            // Back up 1.5 s, before the leader, for BASIC's motor-start delay.
            const start = Math.max(from, syncAt - Math.round(TAPE_RATE * 1.5));
            if (type === 0 && start > from + TAPE_RATE / 10) return start;
            typeBits = -1;
          }
        } else {
          reg = (reg >>> 1) | (bit << 15);
          if ((reg >> 8) === 0x3c && (reg & 0xff) === 0x55) { typeBits = 0; type = 0; syncAt = i; reg = 0; }
        }
      }
    }
    last = v;
  }
  return data.length;
}

function tapeForward() {
  const f = Files.get('tape');
  if (!settings.tape || !f) return;
  const info = tapeInfo();
  const to = nextFileOffset(info.kind, f.data, info.pos);
  vcc.vcc_tape_seek(to);
  renderTape();
  if (to >= f.data.length) toast('No more programs on this tape');
}

// Per emulated frame while a CLOAD started by Run is in progress: RUN goes
// in once the motor has been off for 1.5 s, which is the end of the file.
function tapeAutoRunFrame() {
  if (!tapeAutoRun) return;
  const info = tapeInfo();
  if (info.motor) { tapeAutoRun.seen = true; tapeAutoRun.off = 0; return; }
  if (!tapeAutoRun.seen) { if (++tapeAutoRun.wait > 60 * 30) tapeAutoRun = null; return; }
  if (++tapeAutoRun.off > 90) { tapeAutoRun = null; typeText('RUN\r'); }
}

async function loadTapeProgram(f, run) {
  const tape = await insertTape(f, true);
  if (!tape) return;
  vcc.vcc_tape_mode(1);
  renderTape();
  const first = tapeFirstFile(tape.kind, tape.bytes);
  const what = first ? `"${first.name}"` : f.name;
  if (!run) { toast(`${f.name} is playing: type CLOAD or CLOADM`, 4000); return; }
  if (first && first.type === 2) {
    typeText('CLOADM:EXEC\r');
    toast(`Loading ${what} (machine code)`);
  } else if (first && first.type === 1) {
    toast(`${what} is a data file; the tape is playing for the program that reads it`, 4500);
  } else {
    typeText('CLOAD\r');
    tapeAutoRun = { seen: false, off: 0, wait: 0 };
    toast(`Loading ${what}${first ? '' : ' (no header found; trying CLOAD)'}`, 3500);
  }
}

// ---------------------------------------------------------------- BitBanger printer

// VCC's capture decodes the serial printer bits written to $FF20 into the
// file "printer". A new capture starts a new printout.
let printing = false;
function printerStart() {
  putFile('printer', new Uint8Array(0));
  printing = !!vcc.vcc_printer(1, +settings.prnLF);
  renderPrinter();
  if (printing) toast('Capturing the printer: try LLIST or PRINT#-2,"HELLO"', 3500);
}
function printerStop() { vcc.vcc_printer(0, +settings.prnLF); printing = false; renderPrinter(); }
function printerText() {
  const f = Files.get('printer');
  return f ? new TextDecoder('latin1').decode(f.data).replace(/\r\n?/g, '\n') : '';
}
function renderPrinter() {
  const f = Files.get('printer'), n = f ? f.data.length : 0;
  $('prnStatus').textContent = printing ? `Capturing · ${n} bytes` : n ? `Stopped · ${n} bytes captured` : 'No capture';
}
function printerShow() {
  $('screenText').value = printerText() || '(nothing printed yet)';
  $('textDlg').querySelector('h1').textContent = 'Printout';
  $('menu').classList.remove('open');
  $('textDlg').classList.add('open');
}
function printerSave() {
  const f = Files.get('printer');
  if (!f || !f.data.length) { toast('Nothing printed yet'); return; }
  const stamp = new Date().toISOString().slice(0, 19).replace(/[:T]/g, '-');
  saveToDevice(`vcca-printout-${stamp}.txt`, f.data.slice());
}

function openMenu(section) {
  renderDrives(); renderSlots(); renderHdds(); updateRomLabels(); renderTape(); renderPrinter();
  if (section) { const d = document.querySelector(`details[data-sec=${section}]`); if (d) d.open = true; }
  $('menu').classList.add('open');
}

// ---------------------------------------------------------------- snapshots

// A snapshot is a copy of the emulator's WebAssembly memory, which holds the
// whole machine: CPU, RAM, GIME, PIAs, cartridges, timing. Disks are not in
// it (they live in Files), so restoring one keeps the disks as they are now.
// It only fits the exact build that made it, hence wasmId.
async function gzip(bytes, inflate) {
  if (typeof CompressionStream === 'undefined') return bytes;
  const stream = new Blob([bytes]).stream().pipeThrough(inflate ? new DecompressionStream('gzip') : new CompressionStream('gzip'));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

async function takeSnapshot() {
  const raw = new Uint8Array(mem.buffer).slice();
  return { wasmId, size: raw.length, zipped: typeof CompressionStream !== 'undefined', data: await gzip(raw), date: Date.now(), settings: JSON.parse(JSON.stringify(settings)) };
}

async function restoreSnapshot(snap) {
  if (snap.wasmId !== wasmId) { toast('That save state was made by a different version of VCCA and cannot be loaded', 5000); return false; }
  const raw = snap.zipped ? await gzip(snap.data, true) : snap.data;
  const have = mem.buffer.byteLength;
  if (raw.length > have) mem.grow(Math.ceil((raw.length - have) / 65536));
  new Uint8Array(mem.buffer).set(raw);
  // The disks inserted now stay inserted: point the drives at them again.
  for (let d = 0; d < 4; d++) if (settings.drives[d] && Files.has('drive' + d)) withStr('drive' + d, (np) => vcc.vcc_mount_disk(d, np));
  for (const m of latched) setLook(m, false);
  latched.clear();
  blit();
  return true;
}

async function quickSnapshot() {
  const snap = await takeSnapshot();
  await Store.put('snap:quick', snap);
  toast('State saved. Load state brings you back to this moment.', 3000);
}
async function quickRestore() {
  const snap = await Store.get('snap:quick');
  if (!snap) { toast('No save state yet: tap Save state first'); return; }
  if (await restoreSnapshot(snap)) toast('State loaded');
}

// File format: "VCCASNAP" + 4-byte header length + JSON header + data.
async function saveSnapshotFile() {
  const snap = await takeSnapshot();
  const header = enc.encode(JSON.stringify({ wasmId: snap.wasmId, size: snap.size, zipped: snap.zipped, date: snap.date }));
  const out = new Uint8Array(12 + header.length + snap.data.length);
  out.set(enc.encode('VCCASNAP'));
  new DataView(out.buffer).setUint32(8, header.length, true);
  out.set(header, 12);
  out.set(snap.data, 12 + header.length);
  const stamp = new Date().toISOString().slice(0, 19).replace(/[:T]/g, '-');
  saveToDevice(`vcca-${stamp}.vccasnap`, out);
}
async function loadSnapshotFile() {
  const f = await pickFile('');
  if (!f) return;
  const b = f.bytes;
  if (new TextDecoder().decode(b.subarray(0, 8)) !== 'VCCASNAP') { toast('That is not a VCCA save state'); return; }
  const len = new DataView(b.buffer, b.byteOffset).getUint32(8, true);
  const header = JSON.parse(new TextDecoder().decode(b.subarray(12, 12 + len)));
  header.data = b.slice(12 + len);
  if (await restoreSnapshot(header)) toast('State loaded from ' + f.name);
  $('menu').classList.remove('open');
}

// ---------------------------------------------------------------- screenshots and video

function screenshot() {
  canvas.toBlob(async (blob) => {
    const stamp = new Date().toISOString().slice(0, 19).replace(/[:T]/g, '-');
    saveToDevice(`vcca-${stamp}.png`, new Uint8Array(await blob.arrayBuffer()));
  }, 'image/png');
}

let recorder = null, recordTimer = null;
// MP4 only with H.264 inside: asked for plain "video/mp4", Chromium puts
// VP9 in the MP4, which Windows' players and many Android galleries refuse
// to open. Without H.264, record WebM (VP8 plays in the most players).
function pickVideoType() {
  const types = ['video/mp4;codecs="avc1.42E01E,mp4a.40.2"', 'video/mp4;codecs="avc1.4D401F,mp4a.40.2"', 'video/mp4;codecs="avc1.640028,mp4a.40.2"',
    'video/mp4;codecs=avc1.42E01E', 'video/mp4;codecs=avc1', 'video/webm;codecs=vp8,opus', 'video/webm;codecs=vp8', 'video/webm'];
  return types.find((t) => window.MediaRecorder && MediaRecorder.isTypeSupported(t)) || '';
}
async function toggleRecording() {
  if (recorder) { recorder.stop(); return; }
  if (!window.MediaRecorder || !canvas.captureStream) { toast('This device cannot record video'); return; }
  await startAudio();
  const stream = canvas.captureStream(60);
  if (audioCtx && audioNode) {
    const dest = audioCtx.createMediaStreamDestination();
    audioNode.connect(dest);
    for (const t of dest.stream.getAudioTracks()) stream.addTrack(t);
  }
  const type = pickVideoType();
  const chunks = [];
  recorder = new MediaRecorder(stream, type ? { mimeType: type, videoBitsPerSecond: 4000000 } : {});
  recorder.ondataavailable = (e) => { if (e.data.size) chunks.push(e.data); };
  recorder.onstop = async () => {
    clearTimeout(recordTimer);
    recorder = null;
    $('btnRec').classList.remove('rec');
    $('btnRecMenu').textContent = '⏺ Record video';
    const blob = new Blob(chunks, { type: type || 'video/webm' });
    if (blob.size < 1024) { toast('Nothing was recorded: record for at least a second'); return; }
    const ext = (type || 'video/webm').includes('mp4') ? 'mp4' : 'webm';
    const stamp = new Date().toISOString().slice(0, 19).replace(/[:T]/g, '-');
    saveToDevice(`vcca-${stamp}.${ext}`, new Uint8Array(await blob.arrayBuffer()));
    if (ext !== 'mp4') toast('Saved as WebM: this device cannot record MP4 (H.264). VLC and web browsers play WebM.', 5000);
  };
  recorder.start(1000);
  $('btnRec').classList.add('rec');
  $('btnRecMenu').textContent = '⏹ Stop recording';
  toast('Recording: tap ⏺ again to stop (3 minutes at most)');
  recordTimer = setTimeout(() => { if (recorder) recorder.stop(); }, 180000);
}

// ---------------------------------------------------------------- Commands tab

// [label, text]: \r is ENTER. Commands that need a name or number leave the
// cursor after them.
const QUICK_COMMANDS = [
  ['LOADM"', 'LOADM"'], ['RUN"', 'RUN"'], ['LIST', 'LIST\r'], ['DIR', 'DIR\r'], ['RUN', 'RUN\r'],
  ['EXEC', 'EXEC\r'], ['PRINT', 'PRINT '], ['GOTO', 'GOTO '], ['GOSUB', 'GOSUB '], ['RETURN', 'RETURN'],
  ['CLOAD', 'CLOAD\r'], ['CLOADM', 'CLOADM\r'], ['CSAVE"', 'CSAVE"'],
];

function buildCommands() {
  const box = $('cmdGrid');
  box.innerHTML = '';
  for (const [label, text] of QUICK_COMMANDS) {
    const b = document.createElement('button');
    b.className = 'cmd';
    b.textContent = label;
    b.onclick = () => { startAudio(); typeText(text); };
    box.appendChild(b);
  }
  // User slots 10-16: tap types the saved text, a long press edits it.
  for (let n = 10; n <= 16; n++) {
    const b = document.createElement('button');
    b.className = 'cmd slotcmd';
    const text = settings.cmdSlots[n];
    b.textContent = text ? text.replace(/\|/g, '⏎') : `Slot ${n}`;
    b.title = text ? `Slot ${n}: hold to change` : `Slot ${n}: tap to set`;
    let timer = null, held = false;
    b.addEventListener('pointerdown', () => { held = false; timer = setTimeout(() => { held = true; editSlot(n); }, 1200); });
    const cancel = () => clearTimeout(timer);
    b.addEventListener('pointerup', () => {
      cancel();
      if (held) return;
      if (!settings.cmdSlots[n]) editSlot(n);
      else { startAudio(); typeText(settings.cmdSlots[n].replace(/\|/g, '\r')); }
    });
    b.addEventListener('pointerleave', cancel);
    b.addEventListener('pointercancel', cancel);
    box.appendChild(b);
  }
}

function editSlot(n) {
  const v = prompt(`Slot ${n}: text to type. Use | for ENTER (for example LOADM"GAME":EXEC|). Leave empty to clear.`, settings.cmdSlots[n] || '');
  if (v === null) return;
  if (v.trim()) settings.cmdSlots[n] = v; else delete settings.cmdSlots[n];
  saveSettings();
  buildCommands();
}

// Hooks the Android shell calls.
window.vccBack = () => {
  const open = document.querySelector('.dialog.open');
  if (open && open.id !== 'welcome') { open.classList.remove('open'); return true; }
  if ($('menu').classList.contains('open')) { $('menu').classList.remove('open'); return true; }
  return false;
};
window.vccPause = (p) => {
  if (p) { saveDirtyDisks(); if (audioCtx) audioCtx.suspend(); }
  else if (audioCtx) audioCtx.resume();
  appPaused = p;
};
let appPaused = false;

// ---------------------------------------------------------------- start

(async function main() {
  loadSettings();
  migrateSettings();
  if (!Array.isArray(settings.drives) || settings.drives.length !== 4) settings.drives = [null, null, null, null];
  if (!Array.isArray(settings.slots) || settings.slots.length !== 4) settings.slots = defaults.slots;
  if (!Array.isArray(settings.hdd) || settings.hdd.length !== 2) settings.hdd = [null, null];
  wireUi();
  await Store.open();
  try {
    await loadWasm();
  } catch (e) {
    $('status').textContent = 'Could not start the emulator: ' + e.message;
    return;
  }
  await bootMachine();
  requestAnimationFrame(tick);
})();
