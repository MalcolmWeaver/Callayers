async function showRelease(){
 const status=document.getElementById('release-status'),link=document.getElementById('installer-link');
 if(!status||!link)return;
 try{
  const response=await fetch('https://downloads.callayers.com/tester/latest.json',{cache:'no-store',signal:AbortSignal.timeout(8000)});
  if(!response.ok)return;
  const envelope=await response.json(),bytes=Uint8Array.from(atob(envelope.payload),c=>c.charCodeAt(0));
  const release=JSON.parse(new TextDecoder().decode(bytes));
  if(release.schema!==1||release.channel!=='tester'||!/^\d+\.\d+\.\d+$/.test(release.version)||release.url!==`https://downloads.callayers.com/releases/${release.version}/CallayersSetup-${release.version}.exe`)return;
  link.href=release.url;link.hidden=false;
  status.textContent=`Tester version ${release.version} · Windows x64`;
 }catch{ /* The page remains useful before publication or while offline. */ }
}
showRelease();
