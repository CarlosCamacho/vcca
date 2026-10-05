// Where the test harnesses find files. ROMs are copyrighted and never in the
// repository: put coco3.rom and disk11.rom in roms/ (or set VCCA_ROMS).
import os from 'os';
import path from 'path';
import { fileURLToPath } from 'url';
const here = path.dirname(fileURLToPath(import.meta.url));
export const ROMS = (process.env.VCCA_ROMS || path.join(here, '..', 'roms')) + '/';
export const TMP = (process.env.VCCA_TMP || os.tmpdir()) + '/';
