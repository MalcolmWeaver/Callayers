// This fixture defines the small state interface used by build 2.
// The native host addresses the named frame and validates all returned values.
function interactiveCard() {
  const name=document.getElementById('name'),score=document.getElementById('score');
  const time=document.getElementById('time'),toggle=document.getElementById('toggle');
  let count=0,remaining=300,running=false,started=0;
  const seconds=()=>running?Math.max(0,remaining-(performance.now()-started)/1000):remaining;
  const paint=()=>{
    score.textContent=String(count);
    const left=Math.ceil(seconds());time.textContent=Math.floor(left/60)+':'+String(left%60).padStart(2,'0');
    toggle.textContent=running?'Pause':'Start';
    if(running && !left){remaining=0;running=false;toggle.textContent='Start';}
  };
  document.getElementById('add').onclick=()=>{count=Math.min(9999,count+1);paint();};
  document.getElementById('reset').onclick=()=>{count=0;paint();};
  toggle.onclick=()=>{remaining=seconds();running=!running && remaining>0;started=performance.now();paint();};
  document.getElementById('resetTime').onclick=()=>{remaining=300;running=false;paint();};
  window.CallayersCard={
    snapshot:()=>({count,title:name.value,remaining:seconds()}),
    isRunning:()=>running,
    restore:value=>{count=value.count;name.value=value.title;remaining=value.remaining;running=false;paint();},
    blur:()=>{if(document.activeElement)document.activeElement.blur();}
  };
  setInterval(paint,100);paint();
}
documents[0]=`<style>
html,body{overflow:auto} .box{min-height:100%;height:auto;padding:22px}
h1{font-size:28px;margin-bottom:12px} label{display:block;font-size:20px}
input,button{font:20px Segoe UI,sans-serif;border-radius:8px;padding:8px 12px}
input{width:100%;margin:6px 0 12px;border:2px solid #7c8ba5;background:#fff;color:#111}
button{border:1px solid #a8bad2;background:#334461;color:white;cursor:pointer;margin:0 8px 8px 0}
input:focus,button:focus{outline:3px solid #6ee7b7;outline-offset:2px}
.row{display:flex;align-items:center;gap:12px;margin:6px 0} #score,#time{font-size:32px;font-variant-numeric:tabular-nums}
</style><div class="box"><h1>Interactive card</h1><label>Name<input id="name" maxlength="120" value="Your ideas"></label>
<div class="row"><strong id="score">0</strong><button id="add">+1</button><button id="reset">Reset score</button></div>
<div class="row"><strong id="time">05:00</strong><button id="toggle">Start</button><button id="resetTime">Reset timer</button></div>
<p style="font-size:16px">Values save automatically. Reopened timers stay paused.</p></div>`+
'<script>('+interactiveCard.toString()+')()<'+ '/script>';
