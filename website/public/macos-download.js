async function showMacRelease(arch) {
 const link=document.getElementById(`macos-${arch}-link`),status=document.getElementById(`macos-${arch}-status`);
 if(!link||!status)return;
 const label=arch==='arm64'?'Apple silicon':'Intel';
 try {
  const response=await fetch(`https://downloads.callayers.com/macos/${arch}/latest.json`,{cache:'no-store',credentials:'omit',signal:AbortSignal.timeout(8000)});
  if(response.status===404){status.textContent=`${label}: Not published yet.`;return;}
  if(!response.ok)throw Error();
  const text=await response.text();if(text.length>8192)throw Error();
  const release=JSON.parse(text);
  if(release.schema!==1||release.platform!=='macos'||release.arch!==arch||release.notarized!==true||
    !/^\d+\.\d+\.\d+$/.test(release.version)||!/^\d+$/.test(release.build)||!/^\d+\.\d+(?:\.\d+)?$/.test(release.minimumSystemVersion)||
    !/^[a-f0-9]{64}$/.test(release.sha256)||
    release.url!==`https://downloads.callayers.com/macos/releases/${release.version}/${arch}/Callayers-${release.version}-${release.build}-macos-${arch}.zip`)throw Error();
  link.href=release.url;link.dataset.version=release.version;link.hidden=false;
  status.textContent=`${label}: ${release.version} · macOS ${release.minimumSystemVersion} or later`;
 } catch { status.textContent=`${label}: Release status unavailable. Try again shortly.`; }
}
Promise.all(['arm64','x86_64'].map(showMacRelease));
