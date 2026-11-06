#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
RADAR 433 — Moniteur TEMPS RÉEL + identification
------------------------------------------------
Lit le port série, filtre le bruit, et identifie chaque trame captée en la
comparant aux signatures de référence (fichiers .sub du dossier references/).

Lancement :  .venv/Scripts/python.exe moniteur_temps_reel.py
Page      :  http://localhost:8070
"""

import serial
import threading
import json
import time
import os
import statistics
import re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT_HTTP   = 8070
PORT_SERIE  = "COM10"
BAUDS       = 115200
DOSSIER_REF = os.path.join(os.path.dirname(os.path.abspath(__file__)), "references")
SEUIL_BRUIT = 30      # µs : en dessous, c'est de la friture
SEUIL_TRAME = 80      # µs : une trame doit dépasser ça pour être un vrai signal

# ---- Base de signatures (.sub) ---------------------------------------------
def charger_references(dossier):
    refs = []
    if not os.path.isdir(dossier):
        return refs
    for nom in sorted(os.listdir(dossier)):
        if not nom.endswith(".sub"):
            continue
        proto = bits = te = None
        try:
            with open(os.path.join(dossier, nom), encoding="utf-8", errors="replace") as f:
                for ligne in f:
                    if ligne.startswith("Protocol:"):
                        proto = ligne.split(":", 1)[1].strip()
                    elif ligne.startswith("Bit:"):
                        try: bits = int(ligne.split(":", 1)[1].strip())
                        except ValueError: pass
                    elif ligne.startswith("TE:"):
                        try: te = int(ligne.split(":", 1)[1].strip())
                        except ValueError: pass
        except OSError:
            continue
        if proto:
            refs.append({"fichier": nom, "protocole": proto, "bits": bits, "te": te})
    return refs

REFERENCES = charger_references(DOSSIER_REF)

# ---- Analyse d'une trame ----------------------------------------------------
def analyser(durees):
    """Estime la période de bit et identifie la famille du signal."""
    d = [x for x in durees if x >= SEUIL_BRUIT]
    if not d or max(d) < SEUIL_TRAME:
        return None

    # période de bit : la médiane des durées "courtes" (hors la sync longue)
    triees = sorted(d)
    nb_medi = max(2, len(triees) // 2)
    bit_period = int(statistics.median(triees[:nb_medi]))

    a = {
        "nb": len(d),
        "total": sum(d),
        "max": max(d),
        "bit_period": bit_period,
    }

    # identification heuristique contre les références
    a["classe"] = classer(a)
    return a

def classer(a):
    bp  = a["bit_period"]
    nb  = a["nb"]
    mx  = a["max"]

    # Somfy Telis : ~56 bits, période courte
    if nb >= 100 and bp <= 350:
        return "Somfy Telis (56 bits)"
    # code fixe ~24 bits : Princeton / CAME / Avidsen
    if 20 <= nb <= 34 and 180 <= bp <= 500:
        for r in REFERENCES:
            if r["protocole"].lower().startswith(("princeton", "came")):
                return f"{r['protocole']} (code 24 bits)"
        return "code fixe ~24 bits (Princeton/CAME)"
    # sync longue = code roulant probable
    if mx >= 1500:
        return "code roulant probable (sync longue)"
    return "protocole non standard"

# ---- Données partagées ------------------------------------------------------
frames = []
compteur = 0
lock = threading.Lock()
derniere_activite = time.time()

def lecteur_serie():
    global compteur, derniere_activite
    ser = None
    while True:
        try:
            ser = serial.Serial(PORT_SERIE, BAUDS, timeout=1)
            break
        except Exception:
            time.sleep(1)
    while True:
        try:
            ligne = ser.readline().decode("utf-8", "replace").strip()
        except Exception:
            time.sleep(0.05)
            continue
        if not ligne.startswith("["):
            continue
        corps = ligne.split("]", 1)[1] if "]" in ligne else ""
        try:
            durees = [int(x) for x in corps.split()]
        except ValueError:
            continue
        if not durees:
            continue
        with lock:
            compteur += 1
            frames.append({"id": compteur, "d": durees})
            derniere_activite = time.time()
            if len(frames) > 400:
                del frames[:len(frames) - 400]

# ---- Page web ---------------------------------------------------------------
PAGE = """<!DOCTYPE html>
<html lang="fr"><head><meta charset="utf-8"><title>Radar 433 — Temps réel</title>
<style>
  * { box-sizing:border-box; margin:0; padding:0; }
  body { background:#0b0f14; color:#c9d4de;
    font-family:-apple-system,'Segoe UI',Roboto,sans-serif; padding:16px 18px 40px; }
  h1 { font-size:18px; color:#87CEEB; font-weight:600; margin-bottom:2px; }
  .sous { font-size:12px; color:#7d8a97; }
  .stats { display:flex; gap:20px; margin:12px 0 4px; }
  .stats b { color:#87CEEB; font-size:18px; font-weight:600; }
  .stats .titre { color:#7d8a97; font-size:11px; display:block; }
  .leg { display:flex; gap:16px; font-size:11.5px; color:#7d8a97; margin-bottom:14px; }
  .leg span i { width:13px; height:13px; display:inline-block; border-radius:3px;
    margin-right:5px; vertical-align:-2px; }
  .ligne { margin-bottom:10px; }
  .entete { font-size:11px; color:#7d8a97; margin-bottom:2px; display:flex;
    gap:12px; align-items:baseline; flex-wrap:wrap; }
  .entete .idx { color:#87CEEB; font-weight:600; }
  .entete .classe { color:#ffd166; font-weight:600; }
  svg { background:#0b0f14; border:1px solid #1c2631; border-radius:4px; display:block; }
  .vide { color:#7d8a97; font-size:13px; margin-top:26px; }
  .led { width:9px; height:9px; border-radius:50%; background:#3a4652;
    display:inline-block; margin-right:6px; }
  .led.on { background:#5be08a; box-shadow:0 0 8px #5be08a; }
</style></head><body>
<h1>Radar 433 — Moniteur temps réel</h1>
<div class="sous"><span class="led" id="led"></span><span id="statut">en attente…</span></div>
<div class="stats">
  <div><b id="c_total">0</b><span class="titre">trames captées</span></div>
  <div><b id="c_signaux">0</b><span class="titre">signaux réels (filtrés)</span></div>
  <div><b id="c_dernier">—</b><span class="titre">dernier</span></div>
</div>
<div class="leg">
  <span><i style="background:#87CEEB"></i>porteuse</span>
  <span><i style="background:#1b232e; border:1px solid #33404d"></i>silence</span>
  <span><i style="background:#ffd166"></i>étiquette d'identification</span>
</div>
<div class="flux" id="flux"><div class="vide">Attente d'un signal 433 MHz…</div></div>
<script>
  let dernierVu = 0;
  const flux = document.getElementById('flux');
  function barres(durees, w) {
    const total = durees.reduce((a,b)=>a+b,0)||1, ech = w/total;
    let s='', x=0;
    durees.forEach((d,i)=>{ const wi=d*ech; const c=(i%2===0)?'#87CEEB':'#1b232e';
      s += `<rect x="${x.toFixed(1)}" y="2" width="${wi.toFixed(1)}" height="28" fill="${c}"><title>${d} µs</title></rect>`; x+=wi; });
    return `<svg viewBox="0 0 ${w} 32" width="100%"><rect width="${w}" height="32" fill="#0b0f14"/>${s}</svg>`;
  }
  function render(f) {
    const el = document.createElement('div'); el.className='ligne';
    el.innerHTML = `<div class="entete"><span class="idx">#${f.id}</span>
      <span class="classe">${f.classe || '…'}</span>
      <span>${f.nb} impulsions · ${f.total} µs · période ~${f.bp} µs</span></div>
      ${barres(f.d, 1000)}`;
    flux.prepend(el);
    while (flux.children.length > 30) flux.lastChild.remove();
  }
  async function tick() {
    try {
      const r = await fetch('/data'); const j = await r.json();
      document.getElementById('c_total').textContent = j.brutes;
      document.getElementById('c_signaux').textContent = j.total;
      document.getElementById('statut').textContent = j.statut;
      document.getElementById('led').className =
        'led' + (Date.now()/1000 - j.derniere < 4 ? ' on' : '');
      if (j.frames.length) {
        const d = j.frames[j.frames.length-1];
        document.getElementById('c_dernier').textContent = '#'+d.id;
        for (const f of j.frames) if (f.id > dernierVu) { render(f); dernierVu = f.id; }
        const v = flux.querySelector('.vide'); if (v) v.remove();
      }
    } catch(e) {}
  }
  setInterval(tick, 400); tick();
</script></body></html>"""

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/data":
            with lock:
                copie = list(frames)
                brutes = compteur
                act = derniere_activite
            # filtrer le bruit + analyser
            sorties = []
            for f in copie:
                a = analyser(f["d"])
                if a is None:
                    continue
                sorties.append({
                    "id": f["id"], "nb": a["nb"], "total": a["total"],
                    "bp": a["bit_period"], "classe": a["classe"],
                    "d": f["d"][:140],
                })
            payload = {
                "brutes": brutes,
                "total": len(sorties),
                "derniere": act,
                "statut": f"{PORT_SERIE} — {len(REFERENCES)} signatures chargées — écoute en continu",
                "frames": sorties[-80:],
            }
            corps = json.dumps(payload).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(corps)))
            self.end_headers()
            self.wfile.write(corps)
        elif self.path in ("/", "/index.html"):
            corps = PAGE.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(corps)))
            self.end_headers()
            self.wfile.write(corps)
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, *a):
        pass

if __name__ == "__main__":
    threading.Thread(target=lecteur_serie, daemon=True).start()
    print(f"Références chargées : {len(REFERENCES)}")
    srv = ThreadingHTTPServer(("127.0.0.1", PORT_HTTP), Handler)
    print(f"Moniteur temps réel : http://localhost:{PORT_HTTP}")
    srv.serve_forever()