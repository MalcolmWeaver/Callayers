async function showRelease(){
 const status=document.getElementById('release-status'),link=document.getElementById('installer-link');
 if(!status||!link)return;
 try{
  const configResponse=await fetch('/download-config.json',{cache:'no-store',signal:AbortSignal.timeout(8000)});
  if(!configResponse.ok)return;
  const config=await configResponse.json();
  if(!/^[a-z0-9][a-z0-9-]{1,61}[a-z0-9]$/.test(config.bucket))return;
  const base=`https://storage.googleapis.com/${config.bucket}`;
  const response=await fetch(`${base}/tester/latest.json`,{cache:'no-store',signal:AbortSignal.timeout(8000)});
  if(!response.ok)return;
  const envelope=await response.json(),bytes=Uint8Array.from(atob(envelope.payload),c=>c.charCodeAt(0));
  const release=JSON.parse(new TextDecoder().decode(bytes));
  if(release.schema!==1||release.channel!=='tester'||!/^\d+\.\d+\.\d+$/.test(release.version)||release.url!==`${base}/releases/${release.version}/CallayersSetup-${release.version}.exe`)return;
  link.href=release.url;link.hidden=false;
  status.textContent=`Tester version ${release.version} · Windows x64`;
 }catch{ /* The page remains useful before publication or while offline. */ }
}
showRelease();
