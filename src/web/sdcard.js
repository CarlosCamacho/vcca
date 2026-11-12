// The CoCoSDC's SD card, for app.js and the tests. Each file on the card is
// the file "sd:" + its path in the shared Files map (name -> {data, readOnly,
// dirty}), so the emulator opens it like any disk image; folders are kept in
// a set. Paths are upper case, '/'-separated, with no leading slash.
// The sd_* functions are the WebAssembly imports src/port/sdc.cpp calls.
'use strict';

function makeSdCard(Files, putFile, getMemory, onChange) {
  const dirs = new Set();
  const key = (p) => 'sd:' + p;
  const norm = (p) => String(p).toUpperCase().replace(/\\/g, '/').replace(/^\/+|\/+$/g, '');
  const parentOf = (p) => { const i = p.lastIndexOf('/'); return i < 0 ? '' : p.slice(0, i); };
  const isDir = (p) => p === '' || dirs.has(p);
  const isFile = (p) => Files.has(key(p));
  const changed = (...a) => { if (onChange) onChange(...a); };
  const dec = new TextDecoder('latin1');
  const str = (ptr, len) => dec.decode(new Uint8Array(getMemory().buffer, ptr, len));

  const card = {
    dirs,
    norm,
    files() { return [...Files.keys()].filter((k) => k.startsWith('sd:')).map((k) => k.slice(3)).sort(); },
    isDir, isFile,
    // Entries directly in dir: [{name, dir, size}], sorted by name.
    entries(dir) {
      dir = norm(dir);
      if (!isDir(dir)) return null;
      const out = [];
      for (const d of dirs) if (parentOf(d) === dir) out.push({ name: d.slice(dir ? dir.length + 1 : 0), dir: true, size: 0 });
      for (const p of card.files()) if (parentOf(p) === dir) out.push({ name: p.slice(dir ? dir.length + 1 : 0), dir: false, size: Files.get(key(p)).data.length });
      return out.sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));
    },
    add(path, data, readOnly) {
      path = norm(path);
      for (let d = parentOf(path); d && !dirs.has(d); d = parentOf(d)) dirs.add(d);
      putFile(key(path), data, readOnly);
      changed('file', path);
    },
    mkdir(path) {
      path = norm(path);
      if (!path || isDir(path) || isFile(path) || !isDir(parentOf(path))) return -1;
      dirs.add(path);
      changed('dir', path);
      return 0;
    },
    rename(from, to) {
      from = norm(from); to = norm(to);
      if (!from || !to || isDir(to) || isFile(to) || !isDir(parentOf(to))) return -1;
      if (isFile(from)) {
        Files.set(key(to), Files.get(key(from)));
        Files.delete(key(from));
        changed('rename', from, to);
        return 0;
      }
      if (!dirs.has(from)) return -1;
      for (const d of [...dirs]) if (d === from || d.startsWith(from + '/')) { dirs.delete(d); dirs.add(to + d.slice(from.length)); }
      for (const p of card.files()) if (p.startsWith(from + '/')) { Files.set(key(to + p.slice(from.length)), Files.get(key(p))); Files.delete(key(p)); changed('rename', p, to + p.slice(from.length)); }
      changed('dir', to);
      return 0;
    },
    remove(path) {
      path = norm(path);
      if (isFile(path)) { Files.delete(key(path)); changed('delete', path); return 0; }
      if (!path || !dirs.has(path)) return -1;
      if (card.entries(path).length) return -2;
      dirs.delete(path);
      changed('dir', path);
      return 0;
    },
    create(path, size) {
      path = norm(path);
      if (!path || isDir(path) || isFile(path) || !isDir(parentOf(path))) return -1;
      putFile(key(path), new Uint8Array(size), false);
      Files.get(key(path)).dirty = true;
      changed('file', path);
      return 0;
    },
    imports() {
      return {
        sd_list(pathPtr, pathLen, out, cap) {
          const list = card.entries(str(pathPtr, pathLen));
          if (!list) return -1;
          const text = list.map((e) => (e.dir ? `D ${e.name}` : `F ${e.size} ${e.name}`)).join('\n');
          const bytes = new TextEncoder().encode(text).subarray(0, cap);
          new Uint8Array(getMemory().buffer, out, bytes.length).set(bytes);
          return bytes.length;
        },
        sd_mkdir: (p, l) => card.mkdir(str(p, l)),
        sd_rename: (a, al, b, bl) => card.rename(str(a, al), str(b, bl)),
        sd_delete: (p, l) => card.remove(str(p, l)),
        sd_create: (p, l, size) => card.create(str(p, l), size >>> 0),
      };
    },
  };
  return card;
}

globalThis.makeSdCard = makeSdCard;
