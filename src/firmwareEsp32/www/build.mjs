import { copyFileSync, cpSync, existsSync, mkdirSync, rmSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const rootDir = dirname(fileURLToPath(import.meta.url));
const outDirArg = process.argv[2];
const outDir = outDirArg ? resolve(rootDir, outDirArg) : resolve(rootDir, '..', '.webdist');
const startupDir = resolve(rootDir, 'startup');
const appDir = resolve(rootDir, 'app');
const vendorSource = resolve(rootDir, 'node_modules', 'vue', 'dist', 'vue.esm-browser.prod.js');
const vendorDir = resolve(outDir, 'vendor');

if (!existsSync(vendorSource)) {
    throw new Error('Vue runtime not found. Run npm install in src/firmwareEsp32/www first.');
}

rmSync(outDir, { force: true, recursive: true });
mkdirSync(outDir, { recursive: true });
cpSync(startupDir, resolve(outDir, 'startup'), { recursive: true });
cpSync(appDir, resolve(outDir, 'app'), { recursive: true });
mkdirSync(vendorDir, { recursive: true });
copyFileSync(vendorSource, resolve(vendorDir, 'vue.esm-browser.prod.js'));

console.log(`Built web assets into ${outDir}`);
