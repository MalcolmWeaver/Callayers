import { build } from 'esbuild';
import { mkdir, copyFile } from 'node:fs/promises';
await mkdir('web-dist', { recursive: true });
await build({ entryPoints: ['web/app.ts'], outfile: 'web-dist/app.js', bundle: true, format: 'esm', target: 'es2022', minify: true });
await build({ entryPoints: ['web/account.ts'], outfile: 'web-dist/account.js', bundle: true, format: 'esm', target: 'es2022', minify: true });
for (const file of ['login.html','signin.html','signup.html','home.html','style.css']) await copyFile('web/' + file, 'web-dist/' + file);
await copyFile('web/login.html','web-dist/index.html');
