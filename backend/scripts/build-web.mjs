import { build } from 'esbuild';
import { mkdir, copyFile } from 'node:fs/promises';
await mkdir('web-dist', { recursive: true });
await build({ entryPoints: ['web/app.ts'], outfile: 'web-dist/app.js', bundle: true, format: 'esm', target: 'es2022', minify: true });
for (const file of ['login.html','admin.html','style.css']) await copyFile('web/' + file, 'web-dist/' + file);
await copyFile('web/login.html','web-dist/index.html');
