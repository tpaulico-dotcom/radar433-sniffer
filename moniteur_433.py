#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
RADAR 433 — Moniteur graphique
------------------------------
Lit un journal de capture série (lignes « [N] d1 d2 d3 ... » en microsecondes)
et génère une page HTML où chaque trame est dessinée : une barre par impulsion,
porteuse (HAUT) en bleu ciel, silence (BAS) en foncé, largeur proportionnelle
à la durée.

Usage :
    python moniteur_433.py <fichier.log> [fichier_sortie.html]
"""

import sys
import re
import html

CARRIER     = "#87CEEB"   # porteuse = niveau HAUT
SILENCE     = "#1b232e"   # silence = niveau BAS
FOND        = "#0b0f14"
TEXTE       = "#c9d4de"
TEXTE_FAIBLE= "#7d8a97"

FICHIER_ENTREE  = sys.argv[1] if len(sys.argv) > 1 else "capture.log"
FICHIER_SORTIE  = sys.argv[2] if len(sys.argv) > 2 else "moniteur_433.html"

# ---- Récupérer les trames « [N] d1 d2 ... » --------------------------------
def lire_trames(chemin):
    trames = []
    motif = re.compile(r"^\s*\[(\d+)\]\s+(.+)$")
    with open(chemin, encoding="utf-8", errors="replace") as f:
        for ligne in f:
            m = motif.match(ligne)
            if not m:
                continue
            nb = int(m.group(1))
            durees = [int(x) for x in m.group(2).split()]
            if durees:
                trames.append((nb, durees))
    return trames

# ---- Générer le SVG d'une trame -------------------------------------------
def svg_trame(durees, largeur=920, hauteur=46):
    total = sum(durees)
    if total <= 0:
        total = 1
    echelle = largeur / total       # largeur proportionnelle à la durée

    rects = []
    x = 0.0
    # indice pair = HAUT (porteuse), impair = BAS (silence)
    for i, d in enumerate(durees):
        w = d * echelle
        coul = CARRIER if i % 2 == 0 else SILENCE
        rects.append(
            f'<rect x="{x:.1f}" y="3" width="{w:.1f}" height="40" fill="{coul}" '
            f'stroke="none"><title>{d} µs ({ "porteuse" if i % 2 == 0 else "silence" })</title></rect>'
        )
        x += w

    textes = []
    for i, d in enumerate(durees):
        w = d * echelle
        if w > 34:
            centre = sum(durees[:i]) * echelle + w / 2
            coul_texte = "#0b0f14" if i % 2 == 0 else "#7d8a97"
            textes.append(
                f'<text x="{centre:.1f}" y="27" font-size="11" fill="{coul_texte}" '
                f'text-anchor="middle" font-family="monospace">{d}</text>'
            )

    return f'<rect x="0" y="0" width="{largeur}" height="{hauteur}" fill="{FOND}"/>' \
           + "".join(rects) + "".join(textes)

def x_cum(i, durees, echelle):
    return sum(durees[:i]) * echelle

# ---- Page ------------------------------------------------------------------
def generer(trames):
    lignes = []
    for idx, (nb, durees) in enumerate(trames):
        total = sum(durees)
        lignes.append(
            f'<div class="ligne">'
            f'<div class="entete"><span class="idx">#{idx + 1}</span>'
            f'<span class="meta">{nb} impulsions · {total} µs</span></div>'
            f'<svg viewBox="0 0 920 46" width="920" height="46">'
            f'{svg_trame(durees)}</svg>'
            f'</div>'
        )

    corps = "\n".join(lignes) if lignes else '<div class="vide">Aucune trame — capture encore en cours ou vide.</div>'

    return f"""<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="utf-8">
<title>Radar 433 — Moniteur</title>
<style>
  * {{ box-sizing: border-box; margin: 0; padding: 0; }}
  body {{
    background: {FOND};
    color: {TEXTE};
    font-family: -apple-system, 'Segoe UI', Roboto, sans-serif;
    padding: 18px 20px 40px;
  }}
  h1 {{
    font-size: 19px; font-weight: 600; letter-spacing: .3px; margin-bottom: 6px;
    color: {CARRIER};
  }}
  .sous {{ color: {TEXTE_FAIBLE}; font-size: 12.5px; margin-bottom: 20px; }}
  .legende {{
    display: flex; gap: 18px; font-size: 12px; color: {TEXTE_FAIBLE};
    margin-bottom: 16px; align-items: center;
  }}
  .legende .pastille {{ width: 14px; height: 14px; display: inline-block;
    border-radius: 3px; margin-right: 6px; vertical-align: -2px; }}
  .ligne {{ margin-bottom: 16px; }}
  .entete {{ display: flex; gap: 14px; font-size: 12px; margin-bottom: 3px;
    color: {TEXTE_FAIBLE}; }}
  .idx {{ color: {CARRIER}; font-weight: 600; }}
  svg {{ background: {FOND}; border: 1px solid #1c2631; border-radius: 4px; }}
  .vide {{ color: {TEXTE_FAIBLE}; font-size: 14px; margin-top: 30px; }}
</style>
</head>
<body>
  <h1>Radar 433 — Moniteur de signal</h1>
  <div class="sous">{len(trames)} trame(s) · bleu = porteuse · foncé = silence · largeur = durée en µs</div>
  <div class="legende">
    <span><span class="pastille" style="background:{CARRIER}"></span>porteuse (HAUT)</span>
    <span><span class="pastille" style="background:{SILENCE}"></span>silence (BAS)</span>
  </div>
  {corps}
</body>
</html>"""

# ---- Main ------------------------------------------------------------------
if __name__ == "__main__":
    trames = lire_trames(FICHIER_ENTREE)
    page = generer(trames)
    with open(FICHIER_SORTIE, "w", encoding="utf-8") as f:
        f.write(page)
    print(f"{len(trames)} trame(s) -> {FICHIER_SORTIE}")