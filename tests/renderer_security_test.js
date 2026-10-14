const http=require('http'),{spawn}=require('child_process'),assert=require('assert');
let requests=0;const server=http.createServer((req,res)=>{requests++;res.end('synthetic');});
server.listen(0,'127.0.0.1',()=>{const child=spawn(process.argv[2],[`http://127.0.0.1:${server.address().port}`],{stdio:['ignore','pipe','pipe']});
 let output='';child.stdout.on('data',x=>output+=x);child.stderr.on('data',x=>output+=x);
 const timer=setTimeout(()=>child.kill(),15000);
 child.on('error',()=>{clearTimeout(timer);server.close();process.exitCode=1;});
 child.on('exit',code=>{clearTimeout(timer);server.close();try{assert.equal(code,0,output);assert.equal(requests,0,'Generated frame reached external HTTP');console.log(output.trim());console.log('CSP blocked fetch, synchronous XHR, images and nested frames');}catch(e){console.error(e.message);process.exitCode=1;}});
});
