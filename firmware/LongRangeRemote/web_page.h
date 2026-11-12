// web_page.h - single-page phone UI served by the remote (no internet / CDN needed).
#pragma once
#include <pgmspace.h>

static const char WEB_PAGE[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<meta name="theme-color" content="#16181d">
<title>Long-Range Remote</title>
<style>
:root{--bg:#16181d;--panel:#20232a;--btn:#2c3039;--btn2:#383d49;--fg:#eef0f4;--mut:#9aa1ad;--acc:#e0a526;--red:#d9534f;--ok:#3fa66b}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.35 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;padding-bottom:64px}
header{position:sticky;top:0;z-index:5;background:var(--bg);padding:10px 14px 6px;border-bottom:1px solid #2a2e37}
#dev{font-weight:700;font-size:18px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
#devsub{color:var(--mut);font-size:13px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
#favs{display:flex;gap:6px;overflow-x:auto;padding:8px 0 2px}
.chip{flex:none;border:1px solid var(--btn2);background:var(--panel);color:var(--fg);border-radius:16px;padding:5px 11px;font-size:13px;max-width:180px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.chip.on{border-color:var(--acc);color:var(--acc)}
nav{display:flex;gap:4px;padding:8px 10px;overflow-x:auto;background:var(--bg)}
nav button{flex:1 0 auto;background:none;border:0;color:var(--mut);padding:8px 10px;border-radius:8px;font-size:14px}
nav button.on{background:var(--panel);color:var(--fg)}
main{padding:4px 12px}
section{display:none}section.on{display:block}
h3{margin:18px 2px 8px;font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:var(--mut)}
.grid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}
.grid.g3{grid-template-columns:repeat(3,1fr)}
.k{background:var(--btn);color:var(--fg);border:0;border-radius:12px;min-height:52px;padding:6px 4px;font-size:15px;font-weight:600;touch-action:manipulation;user-select:none;-webkit-user-select:none}
.k:active,.k.on{background:var(--acc);color:#111}
.k.pwr{background:#3a2326;color:#ff8c86}.k.pwr.on{background:var(--red);color:#fff}
.k.big{min-height:64px;font-size:22px}
.k.red{background:#5a2226}.k.green{background:#1f4a2c}.k.yellow{background:#5a5020}.k.blue{background:#1f3360}
.dpad{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;max-width:260px;margin:0 auto}
.dpad .k{min-height:58px}
.list{display:flex;flex-direction:column;gap:6px}
.row{display:flex;align-items:center;gap:8px;background:var(--panel);border-radius:10px;padding:11px 12px}
.row .t{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.row .s{color:var(--mut);font-size:12px;flex:none}
.irow .t{flex:none;max-width:45%}.irow .s{flex:1;min-width:0;text-align:right;white-space:normal;overflow-wrap:anywhere}
.badge{font-size:11px;background:var(--btn2);border-radius:6px;padding:2px 6px;color:var(--mut);flex:none}
input,select{width:100%;background:var(--panel);color:var(--fg);border:1px solid var(--btn2);border-radius:10px;padding:11px;font-size:16px}
label{display:block;color:var(--mut);font-size:13px;margin:10px 2px 4px}
.acts{display:flex;gap:8px;flex-wrap:wrap;margin:10px 0}
.b{background:var(--btn2);color:var(--fg);border:0;border-radius:10px;padding:11px 14px;font-size:15px;font-weight:600}
.b:disabled{opacity:.4}.b.pri{background:var(--acc);color:#111}.b.ok{background:var(--ok);color:#fff}.b.del{background:#4a2a2c;color:#ffb3ae}
.mut{color:var(--mut);font-size:13px}
.codes{display:grid;grid-template-columns:repeat(auto-fill,minmax(104px,1fr));gap:6px}
.codes .k{min-height:44px;font-size:13px;font-weight:500;overflow:hidden;text-overflow:ellipsis}
.scanbox{background:var(--panel);border-radius:14px;padding:16px;text-align:center;margin-top:12px}
.scanbox .n{font-size:34px;font-weight:700}
footer{position:fixed;left:0;right:0;bottom:0;background:#0f1114;border-top:1px solid #2a2e37;padding:9px 14px;font-size:13px;color:var(--mut);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.empty{color:var(--mut);padding:30px 8px;text-align:center}
.up{background:var(--panel);border-radius:10px;padding:12px;margin:8px 0;display:grid;gap:8px}
.up input[type=file]{padding:8px;font-size:14px}
.chk{display:flex;align-items:flex-start;gap:8px;margin:0;color:var(--fg);font-size:14px}.chk input{width:auto;margin:2px 0 0;flex:none}
.warn{background:#4a3a1a;color:#ffd98a;border-radius:10px;padding:12px;margin:8px 0}
</style></head><body>
<header>
  <div id="dev">Long-Range Remote</div><div id="devsub">loading...</div>
  <div id="favs"></div>
</header>
<nav id="tabs">
  <button data-t="remote" class="on">Remote</button><button data-t="browse">Browse</button>
  <button data-t="scan">Code search</button><button data-t="manual">Discover</button><button data-t="settings">Settings</button>
</nav>
<main>
<section id="t-remote" class="on"><div id="nodb" class="warn" hidden></div><div id="remote"></div></section>

<section id="t-browse">
  <div id="bw-brands"><input id="bfilter" placeholder="Search brand (e.g. Samsung, Sony, Foxtel, Denon)" autocomplete="off"><div class="list" id="blist" style="margin-top:10px"></div></div>
  <div id="bw-profiles" style="display:none"><div class="acts"><button class="b" id="bback">&larr; Brands</button></div><h3 id="bname"></h3><div class="list" id="plist"></div></div>
  <div id="bw-detail" style="display:none">
    <div class="acts"><button class="b" id="pback">&larr; Back</button></div>
    <h3 id="pname"></h3>
    <div class="acts"><button class="b pri" id="puse">Use this device</button><button class="b" id="padd">Add to my devices</button></div>
    <p class="mut">Tap a key to test it before choosing. Hold to repeat.</p>
    <div class="codes" id="pcodes"></div>
  </div>
</section>

<section id="t-scan">
  <p class="mut">Don't know which code set your TV, receiver or box uses? Pick where to search, the device type and a key, aim the remote at it and step through the candidates until it reacts. Remotes that share the same code are only tried once.</p>
  <label>Search in</label><select id="sbrand"></select>
  <label>Device type</label><select id="stype"></select>
  <label>Test key</label><select id="skey"></select>
  <div class="mut" id="scount" style="margin-top:8px"></div>
  <label>Auto speed</label><select id="sspeed" title="Time between codes when Auto runs"><option value="0.5">one code every 0.5 s</option><option value="0.75">one code every 0.75 s</option><option value="1">one code every 1 s</option><option value="1.5">one code every 1.5 s</option><option value="2">one code every 2 s</option><option value="3">one code every 3 s</option></select>
  <div class="acts"><button class="b pri" id="sload">Start</button></div>
  <div class="scanbox" id="sbox" style="display:none">
    <div class="n" id="spos">0/0</div><div id="sname" style="margin:6px 0 2px;font-weight:600"></div><div class="mut" id="scode"></div>
    <div class="acts" style="justify-content:center"><button class="b" id="sprev">&larr; Prev</button><button class="b pri" id="ssend">Send again</button><button class="b" id="snext">Next &rarr;</button></div>
    <div class="acts" style="justify-content:center;align-items:center"><button class="b" id="sauto">Auto &#9654;</button><button class="b ok" id="suse">It worked &ndash; use this</button></div>
    <div class="mut" style="text-align:center">Device reacted on Auto? Press Stop, then &larr; Prev &ndash; it may have been one of the last few codes.</div>
  </div>
</section>

<section id="t-manual">
  <div id="d-idle">
    <p class="mut">For a device that isn't in the database. The remote tries every address of a protocol with the function numbers that usually mean Power, Volume + and Mute. Aim it at the device and say when it reacts. Then you step through its functions and name the ones that do something.</p>
    <label>Protocol</label><select id="dfam"></select>
    <div class="acts"><button class="b pri" id="dstart">Start the sweep</button></div>
    <details style="margin-top:14px"><summary class="mut">I already know the protocol and address</summary>
      <div class="grid g3" style="margin-top:6px"><div style="grid-column:span 2"><label>Protocol</label><select id="dkfam"></select></div><div><label>Address</label><input id="dkaddr" inputmode="numeric" value="0"></div></div>
      <div class="acts"><button class="b" id="dopen">Step through its functions</button></div>
    </details>
    <details style="margin-top:10px"><summary class="mut">Send one code by hand</summary>
      <p class="mut">Protocol + parameters in IRP/IRDB terms (D = device, S = sub-device, F = function; decimal or 0x hex). Leave S empty for the protocol default.</p>
      <label>Protocol</label><select id="mproto"></select>
      <div class="grid g3" style="margin-top:6px"><div><label>D</label><input id="md" inputmode="numeric" value="0"></div><div><label>S</label><input id="ms" inputmode="numeric" placeholder="default"></div><div><label>F</label><input id="mf" inputmode="numeric" value="0"></div></div>
      <div class="acts"><button class="k big" id="msend" style="flex:1">Send (hold = repeat)</button></div>
    </details>
  </div>
  <div class="scanbox" id="d-sweep" style="display:none">
    <div class="mut" id="dsw-sub"></div>
    <div class="n" id="dsw-fam"></div><div id="dsw-addr" style="font-weight:600"></div>
    <progress id="dsw-bar" max="1" value="0" style="width:100%;margin:10px 0 4px"></progress>
    <div class="mut" id="dsw-eta"></div>
    <p>Aim the remote at the device and watch it. The moment it does <b>anything</b> (on, off, volume, mute):</p>
    <div class="acts" style="justify-content:center"><button class="b ok" id="dreact" style="font-size:18px;padding:16px 22px">It reacted!</button></div>
    <div class="acts" style="justify-content:center"><button class="b" id="dskip">Skip this protocol</button><button class="b del" id="dstop1">Stop</button></div>
  </div>
  <div class="scanbox" id="d-narrow" style="display:none">
    <div class="mut">Which address was it?</div>
    <div class="n" id="dn-fam"></div><div id="dn-addr" style="font-weight:600"></div>
    <div class="mut" id="dn-st" style="margin:6px 0"></div>
    <p class="mut" id="dn-help"></p>
    <div class="acts" style="justify-content:center"><button class="b" id="dnprev">&larr; Previous</button><button class="b pri" id="dnagain">Try again</button><button class="b" id="dnnext">Next &rarr;</button></div>
    <div class="acts" style="justify-content:center"><button class="b ok" id="dnpick">It reacted to this one</button></div>
    <div class="acts" style="justify-content:center"><button class="b" id="dnresume">Carry on sweeping</button><button class="b del" id="dstop2">Stop</button></div>
  </div>
  <div id="d-learn" style="display:none">
    <div class="scanbox">
      <div class="mut" id="dl-title"></div>
      <div class="mut">function</div><div class="n" id="dl-func"></div>
      <div class="mut" id="dl-pos"></div>
      <div id="dl-guess" style="margin:8px 0;font-weight:600"></div>
      <div class="acts" style="justify-content:center"><button class="b" id="dlprev">&larr; Previous</button><button class="b pri" id="dlagain">Send again</button><button class="b" id="dlnext">Next &rarr;</button></div>
      <div class="acts" style="justify-content:center;align-items:center"><span class="mut">or function number</span><input id="dlgoto" inputmode="numeric" style="width:80px"><button class="b" id="dlgo">Send</button></div>
    </div>
    <label>The device did something? Name this function</label>
    <div class="grid g3"><div style="grid-column:span 2"><select id="dlkey"></select></div><div><button class="b ok" id="dlname" style="width:100%">Name it</button></div></div>
    <input id="dltext" placeholder="your own name (when 'Something else' is chosen)" maxlength="19" style="margin-top:6px;display:none">
    <label style="margin-top:12px">Named so far (tap to send, &times; to remove)</label>
    <div class="codes" id="dllist"></div>
    <div class="acts"><button class="b pri" id="dlsave">Save as a device</button><button class="b del" id="dstop3">Throw away</button></div>
    <div class="mut" id="dlmsg"></div>
  </div>
</section>

<section id="t-settings">
  <h3>My devices</h3>
  <p class="mut">The saved remotes shown at the top of the page. Removing one only takes it off this list; it stays in the database and can be added again from Browse or Code search.</p>
  <div class="list" id="mydevs"></div>
  <div class="acts"><button class="b del" id="fclear">Remove all</button></div>
  <h3>SD card: IR codes</h3>
  <p class="mut">All IR codes live on the SD card, so they can be updated without reflashing. <b>/irdb.bin</b> is the code database (made by tools/build_db.py for this firmware). Flipper-format <b>.ir</b> files under <b>/ir</b> are added as your own remotes: the folder holding a file is its brand (<code>/ir/Living room/TV.ir</code>), and a Flipper-IRDB folder name such as <code>TVs</code> or <code>ACs</code> sets the device type. The card is read at power-up; you can also upload here.</p>
  <div class="up"><b>Code database (irdb.bin)</b>
    <input type="file" id="updb" accept=".bin">
    <button class="b pri" id="updbgo">Upload database &amp; restart</button></div>
  <div class="up"><b>Your own remotes (.ir files)</b>
    <input id="upbrand" placeholder="Brand / folder, e.g. Living room (optional)">
    <input type="file" id="upir" accept=".ir" multiple>
    <button class="b" id="upirgo">Upload files</button></div>
  <div class="mut" id="upmsg"></div>
  <div class="acts"><button class="b" id="restart">Restart (load the card again)</button></div>
  <div class="up"><b>Format the SD card</b>
    <span class="mut">Erases everything on the card and formats it FAT32. Use it for a new card, or one the remote can't read.</span>
    <label class="chk"><input type="checkbox" id="fmtkeep" checked><span>Put the code database and home Wi-Fi back afterwards (from the remote's memory, no upload needed). Your own .ir files can't be put back: keep copies.</span></label>
    <button class="b del" id="fmtgo">Format SD card&hellip;</button>
    <div class="mut" id="fmtmsg"></div></div>
  <div id="lpbox" style="display:none">
  <h3>LED power</h3>
  <p class="mut">Lowers the LED current (the carrier's on-time) to save the AA cells at close range. Use <b>Full</b> for long range. Stored on the remote.</p>
  <div class="acts" id="lpbtns"></div>
  </div>
  <h3>Home Wi-Fi</h3>
  <p class="mut">The remote always runs its own network "LR-Remote". It also joins your home network and is then at <b>http://lrremote.local</b> from any phone on that network. Choose the network on the remote (<b>Menu &rarr; Wi-Fi network</b>), or edit <b>wifi.txt</b> on the SD card. Connection status is below.</p>
  <h3>Status</h3><div class="list" id="info"></div>
  <h3>Code databases</h3>
  <p class="mut">Flipper-IRDB (github.com/Lucaslhm/Flipper-IRDB, CC0). Codes in protocols without a built-in encoder were rendered with IrpTransmogrifier (harctoolbox.org).<br>
  Contains/accesses irdb by Simon Peter and contributors, used under permission. For licensing details and for information on how to contribute to the database, see https://github.com/probonopd/irdb</p>
</section>
</main>
<footer id="last">Nothing sent yet</footer>

<script>
const $=s=>document.querySelector(s), el=(t,c,x)=>{const e=document.createElement(t);if(c)e.className=c;if(x!=null)e.textContent=x;return e};
let META=null, BRANDS=[], STATE=null, KEYIDX={}, CUR=null, DETAIL=null, SCAN=[], SPOS=0, AUTO=null;
// All requests go through one promise chain so "down" always reaches the remote before "up".
let chain=Promise.resolve();
// Every request gives up after a while: one lost on a flaky phone connection must not hold up the
// ones queued behind it (key presses) or leave a list loading forever.
const tfetch=(u,ms,o)=>{const c=new AbortController(),t=setTimeout(()=>c.abort(),ms);return fetch(u,Object.assign({signal:c.signal},o||{})).finally(()=>clearTimeout(t))};
const q=u=>{chain=chain.then(()=>tfetch(u,4000).catch(()=>{}));return chain};
const get=u=>tfetch(u,20000).then(r=>{if(!r.ok)throw r;return r.json()});

// Press-and-hold binding: down -> start, keep-alive every 250 ms, up -> stop.
function hold(btn,url){
  let t=null,on=false;
  const down=e=>{e.preventDefault();if(on)return;on=true;btn.classList.add('on');q(url('down'));t=setInterval(()=>q(url('ka')),250);if(navigator.vibrate)navigator.vibrate(12)};
  const up=()=>{if(!on)return;on=false;btn.classList.remove('on');clearInterval(t);q(url('up'));setTimeout(refreshState,400)};
  btn.addEventListener('pointerdown',down);
  ['pointerup','pointerleave','pointercancel'].forEach(ev=>btn.addEventListener(ev,up));
  btn.addEventListener('contextmenu',e=>e.preventDefault());
}
const codeBtn=(label,code,cls)=>{const b=el('button','k'+(cls?' '+cls:''),label);hold(b,a=>'/api/send?c='+code+'&a='+a);return b};

// ---------- tabs ----------
document.querySelectorAll('#tabs button').forEach(b=>b.onclick=()=>{
  document.querySelectorAll('#tabs button').forEach(x=>x.classList.toggle('on',x==b));
  document.querySelectorAll('section').forEach(s=>s.classList.toggle('on',s.id=='t-'+b.dataset.t));
  if(b.dataset.t=='settings')renderInfo();
  if(b.dataset.t=='manual')disc('state');
});

// ---------- SD card uploads ----------
function upload(url,file,label){
  return new Promise((res,rej)=>{
    const x=new XMLHttpRequest(),fd=new FormData();fd.append('f',file,file.name);
    x.upload.onprogress=e=>{if(e.lengthComputable)$('#upmsg').textContent=label+': '+Math.round(100*e.loaded/e.total)+' %'};
    x.onload=()=>x.status==200?res(x.responseText):rej(x.responseText||('HTTP '+x.status));
    x.onerror=()=>rej('connection lost');x.open('POST',url);x.send(fd)});
}
async function restartRemote(){
  $('#upmsg').textContent='Restarting - the page reloads in about 10 seconds...';
  try{await fetch('/api/restart')}catch(e){}
  setTimeout(()=>location.reload(),10000);
}
$('#updbgo').onclick=async()=>{
  const f=$('#updb').files[0];if(!f)return alert('Choose irdb.bin first');
  try{const r=await upload('/api/upload?kind=db',f,'Uploading '+f.name);$('#upmsg').textContent=r;await restartRemote()}
  catch(e){$('#upmsg').textContent='Not uploaded: '+e}
};
$('#upirgo').onclick=async()=>{
  const fs=[...$('#upir').files];if(!fs.length)return alert('Choose one or more .ir files first');
  const brand=$('#upbrand').value.trim().replace(/[\\/:*?"<>|]/g,'_');let ok=0,errs=[];
  for(const f of fs){
    const path=(brand?brand+'/':'')+f.name;
    try{await upload('/api/upload?kind=ir&path='+encodeURIComponent(path),f,'Uploading '+f.name);ok++}
    catch(e){errs.push(f.name+': '+e)}
  }
  $('#upmsg').textContent=ok+' file(s) saved.'+(errs.length?' Failed: '+errs.join('; '):'')+(ok?' Restart to load them.':'');
};
function renderPower(){
  const b=$('#lpbtns');if(!STATE||!b)return;
  $('#lpbox').style.display=STATE.lp!==undefined?'block':'none';
  if(STATE.lp===undefined)return;b.innerHTML='';
  STATE.lpn.forEach((n,i)=>{const x=document.createElement('button');x.className='b'+(i===STATE.lp?' pri':'');x.textContent=n;
    x.onclick=async()=>{await fetch('/api/power?l='+i);await refreshState();renderInfo()};b.appendChild(x)});
}
$('#restart').onclick=()=>{if(confirm('Restart the remote now?'))restartRemote()};
$('#fmtgo').onclick=async()=>{
  const keep=$('#fmtkeep').checked;
  if(!confirm('Format the SD card?\n\nEVERYTHING on it is erased, including your own .ir files.'+
    (keep?' The code database and home Wi-Fi are written back afterwards.':' The card will be empty: upload irdb.bin before the remote restarts.')))return;
  const b=$('#fmtgo');b.disabled=true;
  $('#fmtmsg').textContent='Formatting'+(keep?' and writing the codes back':'')+' - this takes up to a minute...';
  try{const r=await fetch('/api/format?confirm=FORMAT&keep='+(keep?1:0),{method:'POST'});
    $('#fmtmsg').textContent=(r.ok?'':'Not formatted: ')+await r.text();await refreshState();renderInfo()}
  catch(e){$('#fmtmsg').textContent='Lost the connection while formatting - check the Status screen on the remote'}
  b.disabled=false;
};

// ---------- state / favourites ----------
async function refreshState(){
  try{STATE=await get('/api/state')}catch(e){return}
  const nd=$('#nodb');nd.hidden=STATE.db;
  if(!STATE.db)nd.textContent=STATE.card?('No code database loaded ('+STATE.sd+'). Go to Settings > SD card and upload irdb.bin (it is in the project folder firmware/sd_card).')
    :'No SD card. All IR codes are on the card: copy irdb.bin (project folder firmware/sd_card) to a FAT32 microSD card, insert it and restart.';
  showSpeed(STATE.auto);
  const l=STATE.last;
  $('#last').textContent=l.seq?('Sent: '+l.label+'  ['+l.proto+'] x'+l.frames):'Nothing sent yet';
  const fv=$('#favs');fv.innerHTML='';
  STATE.favs.forEach(([p,b,n])=>{const c=el('button','chip'+(p==STATE.active?' on':''),b+' · '+n);c.onclick=()=>setActive(p);fv.appendChild(c)});
  renderMyDevices();
  updateAddBtn();
  if(!CUR||CUR.p!=STATE.active)loadActive();
}
async function setActive(p){await q('/api/fav?op=active&p='+p);await refreshState()}
const isFav=p=>!!(STATE&&STATE.favs.some(f=>f[0]==p));
async function removeDevice(p){await q('/api/fav?op=del&p='+p);await refreshState()}
// Settings > My devices: one row per saved remote, with Use and Remove buttons.
// Only rebuilt when the list changes, so the 4 s state refresh doesn't eat taps.
let myDevsKey='';
function renderMyDevices(){
  const key=JSON.stringify([STATE.favs,STATE.active]);if(key==myDevsKey)return;myDevsKey=key;
  const L=$('#mydevs');L.innerHTML='';$('#fclear').disabled=!STATE.favs.length;
  if(!STATE.favs.length){L.appendChild(el('div','mut','No saved devices.'));return}
  STATE.favs.forEach(([p,b,n])=>{const r=el('div','row');r.appendChild(el('span','t',b+' · '+n));
    if(p==STATE.active)r.appendChild(el('span','badge','active'));
    else{const u=el('button','b','Use');u.onclick=()=>setActive(p);r.appendChild(u)}
    const d=el('button','b del','Remove');d.onclick=()=>removeDevice(p);r.appendChild(d);L.appendChild(r)});
}
// Browse detail: the second button adds or removes the shown remote.
function updateAddBtn(){if(!DETAIL)return;$('#padd').textContent=isFav(DETAIL.p)?'Remove from my devices':'Add to my devices'}
async function loadActive(){
  if(STATE.active<0){CUR=null;$('#dev').textContent='No device selected';$('#devsub').textContent='Use Browse or Code search to pick your TV, receiver or box';renderRemote();return}
  CUR=await get('/api/codes?p='+STATE.active);
  $('#dev').textContent=CUR.brand;$('#devsub').textContent=CUR.name+' · '+CUR.cat+' · '+CUR.codes.length+' keys';
  renderRemote();
}

// ---------- remote layout ----------
const GROUPS=[
 ['Power',['POWER','POWER_ON','POWER_OFF'],'g3'],
 ['Volume',['VOL_DOWN','MUTE','VOL_UP'],'g3 big'],
 ['Channel',['CH_DOWN','PREV_CH','CH_UP'],'g3 big'],
 ['Navigation','DPAD'],
 ['Colour keys',['RED','GREEN','YELLOW','BLUE']],
 ['TV & video',['GUIDE','LIST','FAV','TEXT','SUBTITLE','AUDIO_LANG','ASPECT','PIC_MODE','APPS','NETFLIX','YOUTUBE','RECORD','TOP_MENU']],
 ['Inputs',['INPUT','IN_HDMI','IN_HDMI2','IN_HDMI3','IN_HDMI4','IN_TV','IN_BD','IN_GAME','IN_CD','IN_TUNER','IN_PHONO','IN_AUX','IN_OPTICAL','IN_COAX','IN_USB','IN_BT','IN_NET','IN_TAPE']],
 ['Playback',['PREV','REW','PLAY','PAUSE','PLAY_PAUSE','STOP','FFWD','NEXT','EJECT','REPEAT','SHUFFLE']],
 ['Sound',['SOUND_MODE','BASS_DOWN','BASS_UP','TREBLE_DOWN','TREBLE_UP','SUB_DOWN','SUB_UP','DIMMER','SLEEP']],
 ['Tuner',['TUNE_DOWN','TUNE_UP','PRESET_DOWN','PRESET_UP']],
 ['Climate',['SPEED_DOWN','SPEED_UP','TEMP_DOWN','TEMP_UP','OSCILLATE','TIMER']],
 ['Numbers',['D1','D2','D3','D4','D5','D6','D7','D8','D9','D0'],'g3'],
];
const COLOUR={RED:'red',GREEN:'green',YELLOW:'yellow',BLUE:'blue'};
function renderRemote(){
  const r=$('#remote');r.innerHTML='';
  if(!CUR){r.appendChild(el('div','empty','No device yet. Open "Browse" to pick your brand and model (TVs, receivers, soundbars, set-top boxes, projectors, players, fans, air-cons...), or "Code search" to find a working code set.'));return}
  const byKey={};CUR.codes.forEach(c=>{if(c[2])byKey[c[2]]=c});
  const lab=id=>META.keys[KEYIDX[id]][1];
  GROUPS.forEach(([title,ids,cls])=>{
    if(ids=='DPAD'){
      const nav=['UP','DOWN','LEFT','RIGHT','OK','MENU','BACK','HOME','SETUP','INFO'].filter(i=>byKey[KEYIDX[i]]);
      if(!nav.length)return;
      r.appendChild(el('h3',null,title));
      const d=el('div','dpad');
      [['MENU','UP','BACK'],['LEFT','OK','RIGHT'],['HOME','DOWN','SETUP']].flat().forEach(i=>{
        const c=byKey[KEYIDX[i]];d.appendChild(c?codeBtn(lab(i).replace('Up','▲').replace('Down','▼').replace('Left','◀').replace('Right','▶'),c[0]):el('span'))});
      r.appendChild(d);
      if(byKey[KEYIDX.INFO]){const g=el('div','grid');g.style.marginTop='8px';g.appendChild(codeBtn('Info',byKey[KEYIDX.INFO][0]));r.appendChild(g)}
      return;
    }
    const have=ids.filter(i=>byKey[KEYIDX[i]]);
    if(!have.length)return;
    r.appendChild(el('h3',null,title));
    const g=el('div','grid'+(cls?' '+cls.split(' ')[0]:''));
    have.forEach(i=>{const c=byKey[KEYIDX[i]];let cl=title=='Power'?'pwr':(COLOUR[i]||'');if(cls&&cls.includes('big'))cl+=' big';
      g.appendChild(codeBtn(i=='VOL_UP'||i=='CH_UP'?'+':i=='VOL_DOWN'||i=='CH_DOWN'?'−':lab(i),c[0],cl))});
    r.appendChild(g);
  });
  r.appendChild(el('h3',null,'All keys ('+CUR.codes.length+')'));
  const f=el('input');f.placeholder='Filter keys...';r.appendChild(f);
  const all=el('div','codes');all.style.marginTop='8px';r.appendChild(all);
  const draw=()=>{all.innerHTML='';const t=f.value.toLowerCase();CUR.codes.filter(c=>!t||c[1].toLowerCase().includes(t)).forEach(c=>{const b=codeBtn(c[1],c[0]);b.title=c[3];all.appendChild(b)})};
  f.oninput=draw;draw();
}

// ---------- browse ----------
function renderBrands(){
  const t=$('#bfilter').value.toLowerCase(),L=$('#blist');L.innerHTML='';
  BRANDS.forEach(([n,cnt],i)=>{if(t&&!n.toLowerCase().includes(t))return;
    const r=el('div','row');r.appendChild(el('span','t',n));r.appendChild(el('span','s',cnt+(cnt>1?' remotes':' remote')));r.onclick=()=>openBrand(i);L.appendChild(r)});
}
async function openBrand(b){
  $('#bw-brands').style.display='none';$('#bw-profiles').style.display='';$('#bw-detail').style.display='none';
  $('#bname').textContent=BRANDS[b][0];const L=$('#plist');L.innerHTML='<div class="empty">loading...</div>';
  const ps=await get('/api/profiles?b='+b);L.innerHTML='';
  ps.forEach(([p,n,cat,cnt,irdb])=>{const r=el('div','row');r.appendChild(el('span','badge',META.cats[cat]));r.appendChild(el('span','t',n));
    r.appendChild(el('span','s',cnt+' keys'));r.onclick=()=>openProfile(p);L.appendChild(r)});
}
async function openProfile(p){
  DETAIL=await get('/api/codes?p='+p);
  $('#bw-profiles').style.display='none';$('#bw-detail').style.display='';
  $('#pname').textContent=DETAIL.brand+' · '+DETAIL.name+' ('+DETAIL.cat+')';
  const C=$('#pcodes');C.innerHTML='';DETAIL.codes.forEach(c=>{const b=codeBtn(c[1],c[0]);b.title=c[3];C.appendChild(b)});
  updateAddBtn();
  window.scrollTo(0,0);
}
$('#bfilter').oninput=renderBrands;
$('#bback').onclick=()=>{$('#bw-profiles').style.display='none';$('#bw-brands').style.display=''};
$('#pback').onclick=()=>{$('#bw-detail').style.display='none';$('#bw-profiles').style.display=''};
$('#puse').onclick=async()=>{await setActive(DETAIL.p);document.querySelector('#tabs button[data-t=remote]').click();window.scrollTo(0,0)};
$('#padd').onclick=async()=>{
  if(isFav(DETAIL.p)){await removeDevice(DETAIL.p);return}
  const r=await fetch('/api/fav?op=add&p='+DETAIL.p);if(!r.ok)alert('List is full - remove a device first (Settings > My devices)');refreshState()};

// ---------- code search ----------
const SCANKEYS=['POWER','POWER_ON','VOL_UP','VOL_DOWN','MUTE','CH_UP','PLAY','INPUT','MENU','IN_CD'];
function showScan(){
  const box=$('#sbox');box.style.display='';
  if(!SCAN.length){$('#spos').textContent='0/0';$('#sname').textContent='No code sets with that key for this brand';$('#scode').textContent='';return}
  const [c,p,b,n,k,sh]=SCAN[SPOS];$('#spos').textContent=(SPOS+1)+' / '+SCAN.length;$('#sname').textContent=b+' · '+n;
  $('#scode').textContent='key: '+k+(sh>1?' · same code on '+sh+' remotes':'');
}
const scanSend=()=>{if(!SCAN.length)return Promise.resolve();const p=q('/api/send?c='+SCAN[SPOS][0]+'&a=tap');setTimeout(refreshState,500);return p};
const scanStep=d=>{if(!SCAN.length)return Promise.resolve();SPOS=(SPOS+d+SCAN.length)%SCAN.length;showScan();return scanSend()};
// Auto: the next code goes out the chosen time after the previous one was sent (never piles up)
let AUTOGEN=0,AUTOLAST=0;
const autoSec=()=>+$('#sspeed').value||1;
const stopAuto=()=>{if(AUTO){clearTimeout(AUTO);AUTO=null;AUTOGEN++;$('#sauto').innerHTML='Auto &#9654;'}};
const autoNext=gen=>{AUTO=setTimeout(()=>autoTick(gen),Math.max(0,AUTOLAST+autoSec()*1000-Date.now()))};
function autoTick(gen){scanStep(1).then(()=>{if(AUTO&&gen==AUTOGEN){AUTOLAST=Date.now();autoNext(gen)}})}
// The speed is saved on the remote and shared with its own code search screen (/api/scanauto)
let SPEEDSET=0;
const showSpeed=ms=>{if(!ms||Date.now()-SPEEDSET<3000)return;const v=String(ms/1000);if([...$('#sspeed').options].some(o=>o.value==v)&&$('#sspeed').value!=v){$('#sspeed').value=v;updateCount()}};
$('#sspeed').value='1';
$('#sspeed').addEventListener('change',()=>{SPEEDSET=Date.now();q('/api/scanauto?ms='+Math.round(autoSec()*1000));updateCount();
  if(AUTO){clearTimeout(AUTO);autoNext(AUTOGEN)}});   // running: the new speed counts from the last code
// "Search in" values: "all", "g<group>", "b<brand>"
const scanUrl=()=>{const v=$('#sbrand').value,t=$('#stype').value;let u='/api/scan?k='+KEYIDX[$('#skey').value]+'&t='+t;
  if(v[0]=='g')u+='&g='+v.slice(1);else if(v[0]=='b')u+='&b='+v.slice(1);return u};
let countReq=0;
async function updateCount(){const id=++countReq;$('#scount').textContent='counting...';
  try{const r=await get(scanUrl());if(id==countReq)$('#scount').textContent=r.length+' different codes to try (~'+Math.max(1,Math.round(r.length*autoSec()/60))+' min on Auto, one every '+autoSec()+' s)'}catch(e){}}
['#sbrand','#stype','#skey'].forEach(s=>$(s).addEventListener('change',()=>{stopAuto();updateCount()}));
$('#sload').onclick=async()=>{stopAuto();SCAN=await get(scanUrl());SPOS=0;showScan();scanSend()};
$('#sprev').onclick=()=>{stopAuto();scanStep(-1)};$('#snext').onclick=()=>{stopAuto();scanStep(1)};$('#ssend').onclick=scanSend;
$('#sauto').onclick=()=>{if(AUTO)return stopAuto();if(!SCAN.length)return;$('#sauto').textContent='Stop';AUTOLAST=Date.now();autoNext(++AUTOGEN)};
$('#suse').onclick=async()=>{stopAuto();if(!SCAN.length)return;await setActive(SCAN[SPOS][1]);document.querySelector('#tabs button[data-t=remote]').click()};

// ---------- manual + discover ----------
hold($('#msend'),a=>{const s=$('#ms').value.trim();return '/api/manual?proto='+encodeURIComponent($('#mproto').value)+'&d='+encodeURIComponent($('#md').value)+'&s='+encodeURIComponent(s===''?'-1':s)+'&f='+encodeURIComponent($('#mf').value)+'&a='+a});
let DISC=null,DFAMS=[],DTIMER=null,DGUESS=-1;
const dShow=m=>['idle','sweep','narrow','learn'].forEach(x=>$('#d-'+x).style.display=x==m?'':'none');
async function disc(q){
  try{DISC=await get('/api/disc?a='+q)}catch(e){return}
  renderDisc();
}
function renderDisc(){
  const d=DISC;if(!d)return;
  dShow(d.mode);
  if(d.restart){$('#dlmsg').textContent='Saved. The remote is restarting - the page reloads in about 10 seconds...';setTimeout(()=>location.reload(),10000);clearInterval(DTIMER);DTIMER=null;return}
  if(d.mode=='sweep'){
    $('#dsw-fam').textContent=d.famName;$('#dsw-addr').textContent=d.addrText;
    $('#dsw-bar').max=d.total;$('#dsw-bar').value=d.pos+1;
    $('#dsw-sub').textContent=d.all?'Protocol '+(d.famPos+1)+' of '+d.famTotal:'Sweeping';
    $('#dsw-eta').textContent='address '+(d.pos+1)+' of '+d.total+', about '+(d.eta>=90?Math.ceil(d.eta/60)+' min':d.eta+' s')+' left';
    $('#dskip').style.display=d.all?'':'none';
  }else if(d.mode=='narrow'){
    $('#dn-fam').textContent=d.famName;$('#dn-addr').textContent=d.addrText+'  ('+(d.pos+1)+' of '+d.total+')';
    $('#dn-st').textContent=d.sending?'trying this address now...':'tried - did the device react?';
    $('#dn-help').textContent=d.done?'That was the last address and nothing reacted so far. Step back with Previous, or stop.':'A reaction shows up late, so this is a few addresses back. Step forward one address at a time until the device reacts again.';
    $('#dnresume').style.display=d.done?'none':'';
  }else if(d.mode=='learn'){
    $('#dl-title').textContent=d.famName+', '+d.addrText;
    $('#dl-func').textContent=d.func;$('#dl-pos').textContent=(d.funcPos+1)+' of '+d.nFuncs+', the likely ones first';
    const mine=d.learned.find(l=>l[0]==d.func);
    $('#dl-guess').textContent=mine?'You named it: '+mine[2]:d.guess?'On other remotes this is usually: '+META.keys[d.guess][1]:'Not used by any remote in the database';
    if(DGUESS!=d.func){DGUESS=d.func;$('#dlkey').value=mine?mine[1]:d.guess;$('#dltext').value=mine&&!mine[1]?mine[2]:'';$('#dltext').style.display=$('#dlkey').value=='0'?'':'none'}
    const L=$('#dllist');L.innerHTML='';
    d.learned.forEach(l=>{const w=el('span','');w.style.cssText='display:flex;gap:4px';const b=el('button','k',l[2]+' ('+l[0]+')');b.style.flex='1';b.onclick=()=>disc('fgoto&f='+l[0]);
      const x=el('button','b del','×');x.onclick=()=>disc('forget&f='+l[0]);w.append(b,x);L.append(w)});
    $('#dlsave').textContent='Save as a device ('+d.learned.length+' key'+(d.learned.length==1?'':'s')+')';$('#dlsave').disabled=!d.learned.length;
    $('#dlmsg').textContent=d.err||('It becomes "'+d.file[1]+'" under '+d.file[0]+'. Saving restarts the remote.');
  }
  // keep following while something is going on (the remote's own screen may be driving it too)
  const live=d.mode!='idle'&&$('#t-manual').classList.contains('on');
  if(live&&!DTIMER)DTIMER=setInterval(()=>{if(!document.hidden)disc('state')},d.mode=='learn'?2500:700);
  if(!live&&DTIMER){clearInterval(DTIMER);DTIMER=null}
}
async function initDisc(){
  try{DFAMS=(await get('/api/disc?a=meta')).families}catch(e){return}
  $('#dfam').append(new Option('All of them (about 35 minutes)','-1'));
  DFAMS.forEach((f,i)=>{$('#dfam').append(new Option(f[0]+'  -  '+f[1],i));$('#dkfam').append(new Option(f[0],i))});
  $('#dfam').value='0';
  const K=$('#dlkey');K.append(new Option('Something else (type a name)','0'));
  META.keys.forEach((k,i)=>{if(i)K.append(new Option(k[1],i))});
  K.onchange=()=>$('#dltext').style.display=K.value=='0'?'':'none';
  disc('state');
}
$('#dstart').onclick=()=>disc('sweep&fam='+$('#dfam').value);
$('#dopen').onclick=()=>disc('open&fam='+$('#dkfam').value+'&addr='+encodeURIComponent($('#dkaddr').value));
$('#dreact').onclick=()=>disc('reacted');$('#dskip').onclick=()=>disc('skip');
$('#dnprev').onclick=()=>disc('step&d=-1');$('#dnagain').onclick=()=>disc('step&d=0');$('#dnnext').onclick=()=>disc('step&d=1');
$('#dnpick').onclick=()=>disc('pick');$('#dnresume').onclick=()=>disc('resume');
$('#dlprev').onclick=()=>disc('fstep&d=-1');$('#dlagain').onclick=()=>disc('fstep&d=0');$('#dlnext').onclick=()=>disc('fstep&d=1');
$('#dlgo').onclick=()=>disc('fgoto&f='+encodeURIComponent($('#dlgoto').value));
$('#dlname').onclick=()=>{DGUESS=-1;disc('name&key='+$('#dlkey').value+'&text='+encodeURIComponent($('#dltext').value))};
$('#dlsave').onclick=()=>disc('save');
['#dstop1','#dstop2','#dstop3'].forEach(i=>$(i).onclick=()=>{if(i!='#dstop3'||!DISC.learned.length||confirm('Throw away the keys you named?'))disc('stop')});

// ---------- settings ----------
function renderInfo(){
  renderPower();
  const I=$('#info');I.innerHTML='';if(!STATE)return;
  [['AA pack (LEDs)',STATE.pack>0?(STATE.pack.toFixed(2)+' V · '+STATE.pct+' %'+(STATE.low?' · low: swap in charged cells':'')):'out (no cells in)'],
   ['Screen battery',STATE.scr>=2?(STATE.scr.toFixed(2)+' V · '+STATE.scrpct+' %'):'none / not readable'],
   ['SD card',STATE.sd],
   ['Access point','LR-Remote · '+STATE.ap],['Home Wi-Fi',STATE.sta?(STATE.ssid+' · '+STATE.sta):STATE.ssid?(STATE.ssid+' · '+STATE.sta_status+(STATE.sta_attempts>1?' (attempt '+STATE.sta_attempts+')':'')):'not configured'],
   ['IR output (running)',STATE.irout||'?'],
   ['Database ('+STATE.dbsrc+')',STATE.counts[0]+' brands · '+STATE.counts[1]+' remotes · '+STATE.counts[2]+' codes'+(STATE.own?' · '+STATE.own+' own':'')]]
   .forEach(([a,b])=>{const r=el('div','row irow');r.appendChild(el('span','t',a));r.appendChild(el('span','s',b));I.appendChild(r)});
}

// ---------- boot ----------
(async()=>{
  META=await get('/api/meta');META.keys.forEach(([id],i)=>KEYIDX[id]=i);
  BRANDS=await get('/api/brands');renderBrands();
  const sb=$('#sbrand');sb.appendChild(new Option('All brands','all'));
  const GROUPS=await get('/api/groups');
  const og=document.createElement('optgroup');og.label='Brand groups';
  GROUPS.forEach(([n,nb,nr],i)=>og.appendChild(new Option(n+(nb>1?' ('+nb+' brands, '+nr+' remotes)':' ('+nr+' remotes)'),'g'+i)));sb.appendChild(og);
  const ob=document.createElement('optgroup');ob.label='Single brand';
  BRANDS.forEach(([n,cnt],i)=>ob.appendChild(new Option(n+' ('+cnt+')','b'+i)));sb.appendChild(ob);
  const CATLABEL={TV:'TVs',AVR:'AV receivers',Soundbar:'Soundbars',Projector:'Projectors',SetTop:'Set-top / cable / satellite boxes',
    Streaming:'Streaming boxes',Disc:'Blu-ray / DVD players',VCR:'VCRs',Amp:'Amplifiers & pre-amps',CD:'CD players',Tuner:'Tuners',
    Tape:'Tape decks',MD:'MiniDisc',System:'Music systems',Speaker:'Active speakers',Audio:'Other audio',Console:'Game consoles',
    Monitor:'Monitors & displays',Climate:'Fans, heaters & purifiers',AC:'Air conditioners',Light:'Lighting',Car:'Car audio',
    Camera:'Cameras',Other:'Other'};
  const st=$('#stype');st.appendChild(new Option('Any','-1'));META.cats.forEach((c,i)=>st.appendChild(new Option(CATLABEL[c]||c,i)));
  const sk=$('#skey');SCANKEYS.forEach(k=>sk.appendChild(new Option(META.keys[KEYIDX[k]][1],k)));
  updateCount();
  const mp=$('#mproto');META.protos.forEach(p=>mp.appendChild(new Option(p,p)));
  initDisc();
  await refreshState();
  setInterval(()=>{if(!document.hidden){refreshState().then(()=>{if($('#t-settings').classList.contains('on'))renderInfo()})}},4000);
})();
</script>
</body></html>
)rawliteral";
