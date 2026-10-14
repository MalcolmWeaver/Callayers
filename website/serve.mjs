import http from 'node:http';
import {readFile} from 'node:fs/promises';
const files=new Set(['index.html','styles.css','download.js','privacy.html']);
const types={html:'text/html; charset=utf-8',css:'text/css; charset=utf-8',js:'text/javascript; charset=utf-8'};
http.createServer(async(req,res)=>{
 const pathname=new URL(req.url,'http://localhost').pathname;
 const file=pathname==='/'?'index.html':pathname.slice(1);
 if(!files.has(file)){res.writeHead(404);res.end();return;}
 try{const content=await readFile(new URL(`./dist/${file}`,import.meta.url));res.writeHead(200,{'Content-Type':types[file.split('.').pop()]});res.end(content);}catch{res.writeHead(500);res.end('Build the website first.');}
}).listen(4173,'127.0.0.1',()=>console.log('Website preview: http://127.0.0.1:4173'));
