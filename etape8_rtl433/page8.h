// Page web de l'étape 8 : thème DOS/AMIGA + spectro + décodeur rtl_433_ESP.
// Layout : spectro pleine largeur en haut, captures (gauche) + catalogue (droite) en bas.
const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="fr"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>SNIFFER RAUS · rtl_433</title>
<style>
  :root{--bg:#000055;--panel:#0000a8;--border:#ffffff;--text:#ffffff;--dim:#aaaaaa;--accent:#ffff55;
    --hi:#00aaaa;--hitext:#000000;--btn:#00aaaa;--btntext:#000000;--danger:#ff5555;--ok:#55ff55;--seuil:#87CEEB;--scan:0.12}
  body.theme-atari{--bg:#d4d4d4;--panel:#ffffff;--border:#000000;--text:#000000;--dim:#555555;--accent:#008000;
    --hi:#00cc00;--hitext:#000000;--btn:#e0e0e0;--btntext:#000000;--danger:#cc0000;--ok:#008000;--seuil:#88ff88;--scan:0}
  *{box-sizing:border-box;margin:0;padding:0;font-family:"Courier New",Courier,"Liberation Mono",monospace;
    -webkit-tap-highlight-color:transparent;touch-action:manipulation}
  body{background:var(--bg);color:var(--text);padding:6px;min-height:100vh;overflow-x:hidden}
  .crt{position:fixed;inset:0;background:linear-gradient(rgba(18,16,16,0) 50%,rgba(0,0,0,.4) 50%);background-size:100% 3px;z-index:99;pointer-events:none;opacity:var(--scan)}
  .app{max-width:1200px;margin:0 auto;min-height:100vh;display:flex;flex-direction:column;gap:6px;padding-bottom:60px}
  .dos-box{background:var(--panel);border:2px solid var(--border);position:relative;padding:10px 8px 8px;box-shadow:3px 3px 0 rgba(0,0,0,.6)}
  .dos-title{position:absolute;top:-11px;left:10px;background:var(--panel);padding:0 6px;color:var(--accent);font-weight:bold;font-size:11px;text-transform:uppercase;letter-spacing:.5px;white-space:nowrap}
  .hd{display:flex;justify-content:space-between;align-items:flex-start;gap:8px;padding-top:2px}
  .logos{display:flex;align-items:center;gap:7px;margin-bottom:3px}
  .year{position:absolute;top:8px;right:12px;font-size:11px;color:#87CEEB;letter-spacing:2px;font-weight:bold;z-index:2}
  .titre{display:block;width:100%;max-width:920px;height:auto;image-rendering:pixelated;margin-left:0;margin-right:auto}
  .titre-atari{display:none}
  body.theme-atari .titre-dos{display:none}
  body.theme-atari .titre-atari{display:block}
  .logo-wrap{margin:6px 0 0}
  body.theme-atari .logo-wrap{background:#00cc00;padding:8px}
    .s{height:24px;width:auto;display:block;opacity:.95}
  .hd .sub{font-size:9px;color:var(--dim)}
  .hd .right{text-align:right;font-size:11px;margin-left:auto}
  .st-cap{color:var(--ok);font-weight:bold} .st-cap.off{color:var(--danger)}
  .st-rssi{color:var(--accent)}
  .themes{display:grid;grid-template-columns:1fr 1fr;gap:5px;margin-top:6px;padding-top:6px;border-top:1px solid var(--border)}
  .dos-btn{background:var(--btn);color:var(--btntext);border:1px solid var(--border);font-weight:bold;padding:10px 12px;min-height:40px;font-family:inherit;cursor:pointer;text-transform:uppercase;display:inline-flex;align-items:center;justify-content:center;gap:6px;box-shadow:2px 2px 0 #000;font-size:12px}
  .dos-btn:active{transform:translate(1px,1px);box-shadow:0 0 0 #000;background:var(--accent);color:#000}
  .dos-btn.sm{padding:5px 9px;min-height:32px;font-size:10.5px}
  .dos-btn.rec{background:transparent;color:var(--danger);border:1px solid var(--danger)}
  .vu{position:relative;height:12px;background:var(--bg);border:1px solid var(--border);margin-top:4px}
  .vu .fill{height:100%;width:0%;background:linear-gradient(90deg,#55ff55,#ffff55 55%,#ff5555 88%);transition:width .15s}
  .vu .lbl{position:absolute;top:0;right:5px;font-size:9px;line-height:12px;color:var(--text);font-weight:bold;text-shadow:0 0 2px #000}
  .scope-wrap{position:relative;width:100%;height:15vh;min-height:70px;max-height:150px;background:#000;border:1px solid var(--border);margin-top:5px}
  canvas#scope{position:absolute;inset:0;width:100%;height:100%;display:block}
  .sc-label{position:absolute;top:3px;left:6px;font-size:9px;color:var(--dim)}
  .sc-cut{position:absolute;bottom:3px;left:6px;font-size:9px;color:var(--seuil)}
      .monitor{display:none;flex-direction:row;height:15vh;min-height:70px;max-height:150px;background:#000;border:1px solid var(--border);margin-top:5px;font-size:9.5px;overflow:hidden}
    .mon-left{flex:1;overflow-y:auto;padding:2px 5px;border-right:1px solid var(--border);color:#ffffff;white-space:pre-wrap;line-height:1.25;word-break:break-word}
    .mon-right{flex:1;overflow-y:auto;padding:2px 5px;white-space:pre;line-height:1.25}
    .mon-right .hex{color:#ffffff}
    .mon-right .rec{color:#000;background:#ffff00;font-weight:bold}
    .mon-left .act{color:var(--accent)}
    .mon-left .rx{color:#ffffff}
    .ctrls{display:flex;gap:10px;background:var(--bg);padding:6px 8px;border:1px solid var(--border);margin-top:5px;font-size:11px}
  .ctl{flex:1}
  .ctl label{display:flex;justify-content:space-between;font-weight:bold;margin-bottom:2px;font-size:10px}
  .ctl .v{color:var(--accent)}
  input[type=range]{-webkit-appearance:none;background:var(--bg);border:1px solid var(--border);height:16px;width:100%;border-radius:0}
  input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:22px;height:22px;background:var(--accent);border:2px solid var(--bg);cursor:pointer}
  .acts{display:flex;gap:6px;margin-top:6px}
  .acts .dos-btn{flex:1}
  .man{display:flex;gap:6px;margin-top:6px}
  .man input{flex:1;min-width:0;background:var(--bg);color:var(--text);border:1px solid var(--border);padding:6px 8px;font-family:inherit;font-size:11px}
  .man select{background:var(--bg);color:var(--text);border:1px solid var(--border);font-family:inherit;font-size:11px}
  .bot{flex:1 1 auto;min-height:0;display:flex;gap:6px}
  .bot .dos-box{flex:1 1 0;min-width:0;display:flex;flex-direction:column;overflow:hidden}
  .bot .hint{flex:0 0 auto}
  .bot .stream, .bot .cat-list{flex:1 1 auto;min-height:0;overflow-y:auto;-webkit-overflow-scrolling:touch;touch-action:pan-y;overscroll-behavior:contain;padding-right:4px}
  .cat-list{display:block}
  .cat-list::-webkit-scrollbar{width:10px}
  .cat-list::-webkit-scrollbar-thumb{background:var(--accent);border-radius:5px}
  .cat-list::-webkit-scrollbar-track{background:transparent}
  .imp-btn{width:100%;margin:4px 0}
    .modal{display:none;position:fixed;inset:0;background:rgba(0,0,0,.75);z-index:200;align-items:center;justify-content:center;padding:16px}
    .modal.open{display:flex}
    .modal-box{background:var(--panel);border:2px solid var(--border);padding:16px;max-width:520px;width:100%;max-height:82vh;overflow-y:auto;box-shadow:4px 4px 0 rgba(0,0,0,.7)}
    .modal-title{font-size:12px;letter-spacing:2px;margin-bottom:10px;color:var(--accent);font-weight:bold}
    .modal-box input,.modal-box textarea{width:100%;background:var(--bg);color:var(--text);border:1px solid var(--border);padding:8px;font-family:inherit;font-size:12px;margin-bottom:8px;box-sizing:border-box}
    .modal-box textarea{min-height:110px;resize:vertical}
    .modal-btns{display:flex;gap:8px}
    .cat-head{font-size:11px;font-weight:bold;padding:5px 2px;margin:3px 0 4px;letter-spacing:1px;border-bottom:1px solid}
  .cat-item .mrk{font-style:normal;font-size:10px;opacity:.7}
  .cat-item{border:1px solid var(--border);background:var(--bg);padding:6px 8px;margin-bottom:5px;display:flex;justify-content:space-between;align-items:center;gap:8px}
  .cat-item .nm{font-weight:bold;font-size:11px;color:var(--accent);word-break:break-all}
  .cat-item .btns{display:flex;gap:4px;flex-shrink:0}
  .hint{font-size:10px;color:var(--dim);margin-bottom:5px}
  .stream{display:flex;flex-direction:column;gap:5px;flex:1 1 auto;min-height:0;overflow-y:auto;-webkit-overflow-scrolling:touch;touch-action:pan-y;overscroll-behavior:contain;padding-right:3px}
  .evt{border:1px solid var(--border);background:var(--bg);padding:6px 8px;font-size:11px}
  .evt .h{color:var(--dim)} .evt .m{color:var(--accent);font-weight:bold}
  .evt .strip{margin-top:4px} .evt .strip svg{display:block;width:100%}
  .evt .id{color:#5be08a} .evt .cmd{color:#5be08a} .evt .r{color:var(--dim)}
  .evt .d2{color:#5be08a}
  .touchbar{position:fixed;bottom:0;left:0;width:100%;background:var(--hi);color:var(--hitext);padding:7px;border-top:1px solid var(--border);z-index:50;display:flex;justify-content:space-around;align-items:center;font-weight:bold;font-size:11px}
  .touchbar button{background:transparent;border:none;color:inherit;font-family:inherit;font-weight:bold;font-size:11px}
  #toast{position:fixed;bottom:58px;left:50%;transform:translateX(-50%);background:var(--panel);border:1px solid var(--border);color:var(--text);padding:8px 16px;font-size:12px;opacity:0;transition:opacity .2s;pointer-events:none;z-index:60;max-width:92%;text-align:center}
  #toast.show{opacity:1}
</style></head>
<body>
<div class="crt"></div>
<div class="app">

  <header class="dos-box">
    <span class="dos-title">┌─ SNIFFER RAUS · rtl_433 ─┐</span>
    <div class="logo-wrap"><img src="/logo" class="titre titre-dos" alt="SNIFFER"><img src="/logo2" class="titre titre-atari" alt="RAUS-SNIFFER"></div>
    <div class="hd">
      <span id="statut" style="display:none">connexion…</span>
      <div class="right">
        <div class="st-cap" id="status-cap">● CAPTURE</div>
        <div class="st-rssi" id="rssi-val">-- dBm</div>
      </div>
    </div>
    <div class="themes">
      <button class="dos-btn sm" onclick="setTheme('dos')">DOS</button>
      <button class="dos-btn sm" onclick="setTheme('atari')">ATARI</button>
    </div>
  </header>

  <section id="sec-scope" class="dos-box">
    <span class="dos-title">┌─ SIGNAL 433.92 MHz ─┐</span>
    <div class="vu"><div class="fill" id="vufill"></div><span class="lbl" id="vulabel">--</span></div>
    <div class="scope-wrap">
      <canvas id="scope"></canvas>
      <div class="sc-label">-20 dBm</div>
      <div class="sc-cut" id="cutoff-label">SEUIL : -95 dBm</div>
    </div>
    <div class="monitor" id="monitor">
      <div class="mon-left" id="mon-logs"></div>
      <div class="mon-right" id="mon-hex"></div>
    </div>
    <div class="ctrls">
      <div class="ctl"><label><span>SEUIL</span><span class="v" id="cutoff-val">-95</span></label>
        <input type="range" id="cutoff-slider" min="-120" max="-20" value="-95" oninput="updateDSP()"></div>
      <div class="ctl"><label><span>GAIN</span><span class="v" id="scale-val">80 dB</span></label>
        <input type="range" id="scale-slider" min="20" max="120" value="80" oninput="updateDSP()"></div>
      <div class="ctl"><label><span>TEMPS</span><span class="v" id="temps-val">8 s</span></label>
        <input type="range" id="temps-slider" min="0.1" max="20" step="0.1" value="8" oninput="updateDSP()"></div>
    </div>
    <div class="acts">
      <button id="btn-toggle" class="dos-btn" onclick="toggleCapture()">❚❚ PAUSE</button>
      <button class="dos-btn" onclick="vider()">✕ CLEAR</button>
      <button class="dos-btn" onclick="toggleMonitor()" id="btn-mon">[M] LOG</button>
    </div>
    <div class="man">
      <input type="text" id="man-data" placeholder="HEX durées µs (ex: 1E0 3C) ou BIN 0/1">
      <select id="man-fmt"><option value="hex">HEX</option><option value="bin">BIN</option></select>
      <button class="dos-btn sm" onclick="man()">[F6] ÉMETTRE</button>
    </div>
  </section>

  <div class="bot">
    <section id="sec-decod" class="dos-box">
      <span class="dos-title">┌─ CAPTURES · DÉCODEUR ─┐</span>
      <div class="hint">Toucher une ligne = enregistrer · ▶ = rejouer.</div>
      <div class="stream" id="stream"><div class="hint">En écoute…</div></div>
    </section>

    <section id="sec-presets" class="dos-box">
      <span class="dos-title">┌─ CATALOGUE ─┐</span>
      <div class="hint">▶ renvoie · ✕ efface · 📄 fiche.</div>
      <button class="dos-btn sm imp-btn" onclick="toggleImport()">⤓ IMPORTER UNE TRAME</button>
      <div class="cat-list" id="cat-list">chargement…</div>
    </section>
  </div>

</div>

<footer class="touchbar"><span>2026 · SYNDICAT SLCA</span></footer>
<div id="imp-modal" class="modal">
  <div class="modal-box">
    <div class="modal-title">IMPORTER UNE TRAME</div>
    <input type="text" id="imp-nom" placeholder="nom de la trame">
    <textarea id="imp-data" placeholder="colle : tableau C en µs, 0xHEX, ou binaire"></textarea>
    <div class="modal-btns">
      <button class="dos-btn sm" onclick="importer()">AJOUTER</button>
      <button class="dos-btn sm" onclick="toggleImport()">FERMER</button>
    </div>
  </div>
</div>
<div id="toast"></div>

<script>
let captureOn=true, dernier=0, lastCount=-1, echelle=80, cutoff=-95, temps=8, yMax=-20;
let specBuf=[], newCount=0, newTime=0, sampMs=25, lastPollT=0, virtMono=0;
const scope=document.getElementById("scope"), c2=scope.getContext("2d");
let W=0,H=0;
function fit(){W=scope.parentElement.clientWidth;H=scope.parentElement.clientHeight;scope.width=W;scope.height=H;}
window.addEventListener("resize",fit);fit();
const rH=20;
function mapY(db){const yMin=yMax-echelle;return (db-yMax)/(yMin-yMax)*(H-rH);}
function sc(){return getComputedStyle(document.body);}
function toast(m){const t=document.getElementById("toast");t.textContent=m;t.className="show";clearTimeout(t._h);t._h=setTimeout(function(){t.className="";},1800);}

function drawScope(){
  if(!W||!H)return;
  c2.fillStyle="#000";c2.fillRect(0,0,W,H);
  c2.strokeStyle="rgba(255,255,255,0.08)";c2.lineWidth=1;
  for(let x=0;x<W;x+=25){c2.beginPath();c2.moveTo(x,0);c2.lineTo(x,H);c2.stroke();}
  for(let y=0;y<H-rH;y+=20){c2.beginPath();c2.moveTo(0,y);c2.lineTo(W,y);c2.stroke();}
  const cutY=Math.min(H-1,Math.max(0,mapY(cutoff)));
  c2.strokeStyle=sc().getPropertyValue("--seuil").trim()||"#87CEEB";c2.setLineDash([3,3]);
  c2.beginPath();c2.moveTo(0,cutY);c2.lineTo(W,cutY);c2.stroke();c2.setLineDash([]);
  if(specBuf.length>1){
    c2.strokeStyle="#ffffff";c2.lineWidth=4;c2.beginPath();
    let virtNewest=newCount+(performance.now()-newTime)/sampMs; if(virtNewest<virtMono)virtNewest=virtMono; else virtMono=virtNewest;
    const winLen=temps/0.025; const base=(specBuf.length-1)-newCount;
    for(let x=0;x<W;x++){
      const g=(virtNewest-winLen)+(x/W)*winLen;
      const i=base+g;const i0=Math.max(0,Math.floor(i));const i1=Math.min(specBuf.length-1,i0+1);const fr=i-i0;
      const db=specBuf[i0]+(specBuf[i1]-specBuf[i0])*fr;
      const y=Math.min(H-rH-1,Math.max(0,mapY(db)));
      if(x===0)c2.moveTo(x,y);else c2.lineTo(x,y);
    }
    c2.stroke();
  }
  const spanSec=temps;
  const accent=sc().getPropertyValue("--accent").trim()||"#ffff55";
  c2.fillStyle="rgba(0,0,0,0.45)";c2.fillRect(0,H-rH,W,rH);
  c2.strokeStyle=accent;c2.beginPath();c2.moveTo(0,H-rH+0.5);c2.lineTo(W,H-rH+0.5);c2.stroke();
  const step=(spanSec>=12)?4:(spanSec>=6)?2:1; c2.font="bold 10px monospace"; c2.fillStyle=accent;
  for(let t=0;t<=spanSec-0.001;t+=step){const x=Math.round(W-(t/spanSec)*W);c2.beginPath();c2.moveTo(x,H-rH);c2.lineTo(x,H-rH+5);c2.stroke();c2.fillText("-"+t+"s",x+2,H-rH+15);}
  c2.fillText("0s",W-16,H-rH+15);
  c2.fillStyle=sc().getPropertyValue("--dim").trim()||"#aaa";c2.font="9px monospace";c2.fillText("TEMPS",4,H-rH+6);
  requestAnimationFrame(drawScope);
}
drawScope();

function append(j){
  if(!j.histo)return;
  if(lastCount<0)lastCount=j.hcount-j.histo.length;
  let n=j.hcount-lastCount;if(n<=0)return;if(n>j.histo.length)n=j.histo.length;
  const tail=j.histo.slice(j.histo.length-n);feedHex(tail);
  for(const v of tail)specBuf.push(v);
  if(specBuf.length>512)specBuf=specBuf.slice(specBuf.length-512);
  const now=performance.now(); if(lastPollT>0&&n>0){const m=(now-lastPollT)/n;if(m>4&&m<200)sampMs=sampMs*0.8+m*0.2;} lastPollT=now;
  lastCount=j.hcount; newCount=j.hcount-1; newTime=now;
}

function majCap(){
  const cap=document.getElementById("status-cap");const b=document.getElementById("btn-toggle");
  if(captureOn){cap.textContent="● CAPTURE";cap.className="st-cap";b.textContent="❚❚ PAUSE";}
  else{cap.textContent="○ PAUSE";cap.className="st-cap off";b.textContent="▶ START";}
}
function toggleCapture(){captureOn=!captureOn;majCap();fetch("/capture?on="+(captureOn?1:0)).catch(function(){});}
function man(){const d=document.getElementById("man-data").value.trim();if(!d)return;const f=document.getElementById("man-fmt").value;fetch("/manuel?fmt="+f+"&data="+encodeURIComponent(d)).then(function(){toast("Signal manuel émis");});}

function dessiner(dd){
  if(!dd||!dd.length)return "";
  const t=dd.reduce(function(a,b){return a+b;},0)||1;const ech=1000/t;
  let s="",x=0;
  dd.forEach(function(v,i){const w=v*ech;const c=(i%2===0)?"#87CEEB":"#1b232e";
    s+='<rect x="'+x.toFixed(1)+'" y="2" width="'+w.toFixed(1)+'" height="16" fill="'+c+'"></rect>';x+=w;});
  return '<svg viewBox="0 0 1000 20"><rect width="1000" height="20" fill="#000"/>'+s+'</svg>';
}
function ligneEvt(e){
  const t=document.getElementById("stream");
  const d=document.createElement("div");d.className="evt";
  const strip=(e.d&&e.d.length)?'<div class="strip">'+dessiner(e.d)+'</div>':'';
  const det=(e.model==="BRUT")?(e.id+" imp · "+e.cmd+" ms"):("id "+e.id+" · cmd "+e.cmd);
  d.innerHTML='<div class="head"><button class="dos-btn sm rep" data-n="'+e.no+'">▶</button><span class="h">'+e.heure+'</span> <span class="m">'+e.model+'</span> <span class="d2">'+det+'</span> <span class="r">'+e.rssi+' dBm</span></div>'+strip;
  d.title="Toucher = enregistrer · ▶ = rejouer";
  const b=d.querySelector(".rep");if(b){b.onclick=function(ev){ev.stopPropagation();b.textContent="…";fetch("/rejouer?no="+e.no).then(function(){toast("Rejoué");monLog("REJOUÉ "+(e.model||"?")+" "+e.id);setTimeout(function(){b.textContent="▶";},600);});};}
  d.onclick=function(){const n=prompt("Nommer cette capture :",e.model==="BRUT"?("brut "+e.id+"imp"):(e.model+" id "+e.id));if(n===null)return;const nom=n.trim()||(e.model+" "+e.id);fetch("/enregistrer?nom="+encodeURIComponent(nom)+"&no="+e.no).then(function(){toast("Enregistré : "+nom);monLog("SAUVÉ "+nom);charger();});};
  t.prepend(d);while(t.children.length>40)t.lastChild.remove();
}
function vider(){document.getElementById("stream").innerHTML="";}

function setTheme(name){document.body.className="";if(name==="atari")document.body.classList.add("theme-atari");}
function updateDSP(){
  cutoff=parseInt(document.getElementById("cutoff-slider").value);
  echelle=parseInt(document.getElementById("scale-slider").value);
  temps=parseFloat(document.getElementById("temps-slider").value);
  document.getElementById("cutoff-val").textContent=cutoff+" dBm";
  document.getElementById("scale-val").textContent=echelle+" dB";
  document.getElementById("temps-val").textContent=temps+" s";
  document.getElementById("cutoff-label").textContent="SEUIL : "+cutoff+" dBm";
}

async function tick(){
  try{
    const r=await fetch("/data");const j=await r.json();
    document.getElementById("statut").textContent=j.statut;
    document.getElementById("rssi-val").textContent=(j.rssi!=null?Math.round(j.rssi)+" dBm":"--");
    const v=Math.max(0,Math.min(100,j.niveau||0));
    document.getElementById("vufill").style.width=v+"%";
    document.getElementById("vulabel").textContent=(j.rssi!=null?Math.round(j.rssi)+" dBm":"--");
    if(j.capture!==undefined&&j.capture!==captureOn){captureOn=j.capture;majCap();}
    append(j);
    if(j.events&&j.events.length){for(const e of j.events)logEvent(e);
      const recus=j.events.filter(function(e){return e.no>dernier;}).sort(function(a,b){return a.no-b.no;});
      for(const e of recus){ligneEvt(e);feedMonitor(e);}
      if(recus.length)dernier=Math.max(dernier,j.events.map(function(e){return e.no;}).reduce(function(a,b){return Math.max(a,b);},dernier));
    }
  }catch(e){}
}

async function charger(){
  try{
    const r=await fetch("/catalogue");const j=await r.json();
    const c=document.getElementById("cat-list");
    if(!j.catalogue.length){c.innerHTML="<div class=\"hint\">catalogue vide</div>";return;}
    const CATS=[["Volets & Stores","#87CEEB"],["Portes & Garages","#5be08a"],["Sonnettes","#ffd166"],["Alarmes","#ff5b5b"],["Capteurs / Maison","#b388ff"],["Télécommandes","#ff9d2e"]];
    let html="";
    for(let ci=0;ci<CATS.length;ci++){
      const items=j.catalogue.filter(function(e){return e.cat===ci;});
      if(!items.length)continue;
      html+='<div class="cat-head" style="color:'+CATS[ci][1]+'">▸ '+CATS[ci][0]+'</div>';
      items.forEach(function(e){html+='<div class="cat-item"><span class="nm">'+e.nom+' <i class="mrk">'+e.marque+'</i></span><span class="btns"><button class="dos-btn sm" data-id="'+e.id+'">▶</button></span></div>';});
    }
    const mes=j.catalogue.filter(function(e){return e.cat<0;});
    if(mes.length){
      html+='<div class="cat-head" style="color:#26d3d3">⭐ MES CAPTURES</div>';
      mes.forEach(function(e){html+='<div class="cat-item"><span class="nm">'+e.nom+'</span><span class="btns"><button class="dos-btn sm" data-fiche="'+e.id+'">📄</button><button class="dos-btn sm" data-id="'+e.id+'">▶</button><button class="dos-btn sm rec" data-del="'+e.id+'">✕</button></span></div>';});
    }
    c.innerHTML=html;
    c.querySelectorAll("[data-id]").forEach(function(b){b.onclick=async function(){b.textContent="…";await fetch("/envoyer?id="+b.dataset.id);toast("Signal émis");monLog("ÉMIS id="+b.dataset.id);setTimeout(charger,700);};});
    c.querySelectorAll("[data-del]").forEach(function(b){b.onclick=async function(){await fetch("/supprimer?id="+b.dataset.del);toast("Effacé");charger();};});
    c.querySelectorAll("[data-fiche]").forEach(function(b){b.onclick=function(){window.open("/fiche?id="+b.dataset.fiche,"_blank");};});
  }catch(e){}
}

function toggleImport(){const m=document.getElementById("imp-modal");m.classList.toggle("open");if(m.classList.contains("open")){setTimeout(function(){document.getElementById("imp-data").focus();},60);}}
function importer(){const nom=document.getElementById("imp-nom").value.trim()||("import "+(Date.now()%1000));const d=document.getElementById("imp-data").value;if(!d.trim())return;fetch("/importer?nom="+encodeURIComponent(nom)+"&data="+encodeURIComponent(d)).then(function(r){return r.json();}).then(function(j){if(j.ok){toast("Ajouté : "+nom);monLog("IMPORTÉ "+nom);document.getElementById("imp-data").value="";toggleImport();charger();}else{toast("Échec : "+(j.err||"invalide"));}});}

let hexBuf=[];
function appendHexLine(txt,yellow){const R=document.getElementById("mon-hex");if(!R)return;const d=document.createElement("div");d.className=yellow?"rec":"hex";d.textContent=txt;R.appendChild(d);while(R.children.length>400)R.removeChild(R.firstChild);R.scrollTop=R.scrollHeight;}
function feedHex(samples){if(!monOn)return;for(var i=0;i<samples.length;i++){var v=samples[i];var above=(v>=cutoff);hexBuf.push(above?("0000"+Math.max(0,Math.round(v+200)).toString(16).toUpperCase()).slice(-4):"0000");if(hexBuf.length>=8){appendHexLine(hexBuf.join(" "),false);hexBuf=[];}}}
let monOn=false;
function toggleMonitor(){monOn=!monOn;const sw=document.querySelector(".scope-wrap");const mo=document.getElementById("monitor");if(monOn){sw.style.display="none";mo.style.display="flex";document.getElementById("btn-mon").textContent="[M] SPECTRO";}else{sw.style.display="block";mo.style.display="none";document.getElementById("btn-mon").textContent="[M] LOG";}}
function hexOf(e){const d=e.d||[];let s="";for(var i=0;i<d.length;i++){s+=("0000"+d[i].toString(16).toUpperCase()).slice(-4)+" ";}return s;}
function feedMonitor(e){const rec=(e.model&&e.model!=="BRUT");if(!rec)return;appendHexLine(hexOf(e),true);}
let loggedEvt={};
function logEvent(e){if(loggedEvt[e.no])return;loggedEvt[e.no]=true;const rec=(e.model&&e.model!=="BRUT");const det=rec?(e.model+" id="+e.id+" cmd="+e.cmd+" "+e.rssi+"dBm"):("BRUT "+e.id+"imp "+e.rssi+"dBm");monLog((e.heure||"")+" REÇU "+det,rec);}
function monLog(msg,rec){const L=document.getElementById("mon-logs");if(!L)return;const d=document.createElement("div");d.className=rec?"act":"rx";d.textContent=msg;L.appendChild(d);while(L.children.length>300)L.removeChild(L.firstChild);L.scrollTop=L.scrollHeight;}
setInterval(tick,150);tick();charger();majCap();
</script>
</body></html>
)rawliteral";