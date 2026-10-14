import http from 'node:http';
import {readFile} from 'node:fs/promises';
const files=new Set(['index.html','styles.css','download.js','privacy.html','callayers-mark.svg','callayers-zoom-windows.png']);
const types={html:'text/html; charset=utf-8',css:'text/css; charset=utf-8',js:'text/javascript; charset=utf-8',svg:'image/svg+xml',png:'image/png'};
const server=http.createServer(async(req,res)=>{
 const pathname=new URL(req.url,'http://localhost').pathname;
 const file=pathname==='/'?'index.html':pathname.slice(1);
 if(!files.has(file)){res.writeHead(404);res.end();return;}
 try{const content=await readFile(new URL(`./dist/${file}`,import.meta.url));res.writeHead(200,{'Content-Type':types[file.split('.').pop()]});res.end(content);}catch{res.writeHead(500);res.end('Build the website first.');}
});
let port=4173;
server.on('error',error=>{
 if(error.code==='EADDRINUSE'&&port<4273){
  port+=1;
  server.listen(port,'127.0.0.1');
  return;
 }
 console.error(`Could not start website preview: ${error.message}`);
 process.exitCode=1;
});
server.on('listening',()=>console.log(`Website preview: http://127.0.0.1:${port}`));
server.listen(port,'127.0.0.1');
