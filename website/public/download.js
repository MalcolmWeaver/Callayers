async function showRelease(){
 const status=document.getElementById('release-status'),link=document.getElementById('installer-link');
 if(!status||!link)return;
 const unavailable=reason=>{if(typeof window!=='undefined')window.callayersTrack?.('download_unavailable',{reason});};
 try{
  const configResponse=await fetch('/download-config.json',{cache:'no-store',signal:AbortSignal.timeout(8000)});
  if(!configResponse.ok){unavailable('config_unavailable');return;}
  const config=await configResponse.json();
  if(config.bucket!=='callayers-downloads'||config.baseUrl!=='https://downloads.callayers.com'){unavailable('invalid_config');return;}
  const base=config.baseUrl;
  const response=await fetch(`${base}/tester/latest.json`,{cache:'no-store',signal:AbortSignal.timeout(8000)});
  if(!response.ok){unavailable('release_unavailable');return;}
  const envelope=await response.json(),bytes=Uint8Array.from(atob(envelope.payload),c=>c.charCodeAt(0));
  const release=JSON.parse(new TextDecoder().decode(bytes));
  if(release.schema!==1||release.channel!=='tester'||!/^\d+\.\d+\.\d+$/.test(release.version)||release.url!==`${base}/releases/${release.version}/CallayersSetup-${release.version}.exe`){unavailable('invalid_release');return;}
  link.href=release.url;link.hidden=false;
  if(link.dataset)link.dataset.version=release.version;
  status.textContent=`Tester version ${release.version} · Windows x64`;
 }catch{unavailable('lookup_failed'); /* The page remains useful while offline. */ }
}
showRelease();
