(() => {
 const byId=id=>document.getElementById(id);
 const windows=byId('choose-windows'),linux=byId('choose-linux'),distro=byId('linux-distro');
 if(!windows||!linux||!distro)return;
 // Browsers expose Linux, but cannot reliably identify its distribution or release.
 if(!distro.value)distro.value='ubuntu';
 const nav=typeof navigator==='undefined'?{}:navigator;
 const agent=nav.userAgent??'',platform=nav.userAgentData?.platform??nav.platform??'';
 const mobile=nav.userAgentData?.mobile||/Android|iPhone|iPad|iPod|CrOS/i.test(agent)||(/Mac/i.test(platform)&&nav.maxTouchPoints>1);
 const detected=mobile?'':/Win/i.test(platform)||/Windows NT/i.test(agent)?'windows':/Linux/i.test(platform)||/Linux/i.test(agent)?'linux':'';
 let selected=detected;
 const topLinks=[...document.querySelectorAll('[data-download-cta]')];
 const install='curl -fsSL https://downloads.callayers.com/linux/install.sh | sudo bash';
 const experimental=new Set(['debian','mint','pop','fedora','arch']);
 const native=new Set(['fedora','arch']);
 const fusion=byId('enable-rpmfusion');

 function updateCommand() {
  const release=byId('linux-download'),choice=distro.value;
  const command=byId('linux-command'),panel=byId('linux-command-panel'),note=byId('linux-distro-note');
  const allowed=['ubuntu',...experimental].includes(choice)&&(!experimental.has(choice)||release.dataset.experimental==='true')&&(!native.has(choice)||release.dataset.native==='true');
  byId('fedora-repository').hidden=choice!=='fedora'||!allowed;
  panel.hidden=!allowed||release.hidden;
  command.textContent='';
  byId('copy-linux-status').textContent='Paste it into Terminal and enter your password when asked.';
  if(!choice){note.textContent='Choose your distribution to see its install command.';return;}
  if(choice==='other'){note.textContent='An installer is not available for this distribution yet. Other Linux distributions are not currently supported.';return;}
  if(!allowed){note.textContent='Experimental installation for this distribution is not available in the published release.';return;}
  const versions=release.dataset.ubuntu??'22.04 / 24.04 / 26.04';
  const notes={ubuntu:`Ubuntu ${versions}.`,debian:'Debian 12 or 13. Experimental: desktop and camera setup have not been verified.',
   mint:`Linux Mint with an Ubuntu ${versions} base. Experimental; Linux Mint Debian Edition is not supported.`,
   pop:`Pop!_OS with an Ubuntu ${versions} base. Experimental: desktop and camera setup have not been verified.`,
   fedora:'Fedora 43 or 44 on a traditional desktop. Experimental; Fedora Atomic editions are not supported. The camera requires RPM Fusion Free.',
   arch:'Arch Linux (rolling). Experimental. Installation performs a full system upgrade; a kernel update may require a reboot and rerunning the installer.'};
  note.textContent=notes[choice];
  byId('linux-support-badge').textContent=experimental.has(choice)?'Experimental':'Ubuntu beta';
  command.textContent=install+(experimental.has(choice)?' -s -- --allow-experimental':'')+(choice==='fedora'&&fusion.checked?' --enable-rpmfusion':'');
 }
 function refresh() {
  windows.setAttribute('aria-pressed',String(selected==='windows'));
  linux.setAttribute('aria-pressed',String(selected==='linux'));
  byId('windows-download').hidden=selected!=='windows';
  byId('linux-panel').hidden=selected!=='linux';
  byId('platform-hint').textContent=selected?`${selected===detected?'Selected for your browser':'Selected'}: ${selected==='windows'?'Windows':'Linux'}. You can switch platforms above.`:'Choose a desktop platform. Callayers is available for Windows and supported Linux systems.';
  const installer=byId('installer-link');
  for(const link of topLinks){
   const direct=selected==='windows'&&!installer.hidden;
   link.href=direct?installer.href:'#download';
   link.dataset.downloadPlatform=direct?'windows':'';
   if(direct)link.dataset.version=installer.dataset.version;else delete link.dataset.version;
   link.querySelector('[data-download-label]').textContent=selected==='linux'?'Install for Linux':selected==='windows'?'Download for Windows':'Get Callayers';
  }
  updateCommand();
 }
 for(const [button,value]of [[windows,'windows'],[linux,'linux']])button.addEventListener('click',()=>{selected=value;refresh();});
 distro.addEventListener('change',updateCommand);
 fusion.addEventListener('change',updateCommand);
 for(const link of topLinks)link.addEventListener('click',event=>{
  if(link.dataset.downloadPlatform==='windows')return;
  event.preventDefault();
  byId('download').scrollIntoView({behavior:'auto',block:'start'});
  if(selected==='linux'&&!byId('linux-download').hidden)distro.focus({preventScroll:true});
  else (selected==='linux'?linux:windows).focus({preventScroll:true});
 });
 byId('copy-linux').addEventListener('click',async()=>{
  const release=byId('linux-download'),command=byId('linux-command').textContent;
  if(release.hidden||byId('linux-command-panel').hidden||!command||!release.dataset.version)return;
  const version=release.dataset.version,distribution=distro.value,status=byId('copy-linux-status');
  try{await navigator.clipboard.writeText(command);}
  catch{status.textContent='Select the command above and copy it manually.';return;}
  // Selection may have changed while the clipboard permission prompt was open.
  if(distro.value===distribution)status.textContent='Copied. Paste it into Terminal to install Callayers.';
  try{window.callayersTrack?.('linux_install_command_copied',{platform:'linux',app_version:version,distribution,button_location:'download'});}catch{}
 });
 window.callayersDownloads={refresh};
 refresh();
})();
