# RADAR 433 — Analyseur et rejoueur de signaux 433 MHz

Serveur WiFi intégré, interface en français, affichage sur téléphone ou tablette.

## Le but

- **Écouter** les signaux 433 MHz (télécommandes, portails, volets, carillons, sondes)
- **Décoder** ce qui peut l'être : protocole, code, nombre de bits, longueur d'impulsion
- **Afficher** la liste des signaux captés sur une page web, en français
- **Nommer** chaque signal (« portail », « volet salon ») et le **rejouer**
- **Tout garder en mémoire**, même après coupure de courant

## Matériel

| pièce | rôle |
|---|---|
| ESP32 D0WD-V3 (4 Mo) | le cerveau |
| Récepteur 433 (le petit module) | écoute |
| Émetteur FS1000A (le vert) | rejoue |
| 2 fils de 17,3 cm | antennes — à souder sur RX et TX |

### Câblage prévu

```\nRécepteur :  GND -> GND     VCC -> 3V3    DATA -> GPIO 4\nÉmetteur  :  GND -> GND     VCC -> 3V3    DATA -> GPIO 5\n```

**Règles de sécurité (dues à une nuit entière de cartes perdues) :**

1. **Jamais de câblage sous tension.** USB débranché, tous les fils en place, puis on branche.
2. **Aucun 5 V dans le montage.** Les modules fonctionnent en 3,3 V. La sortie de données du
   récepteur reste ainsi à 3,3 V et la carte est protégée.
3. Un fil sur **GPIO 0** ou **GPIO 1 (TXD)** empêche tout flash : à débrancher si la carte
   devient muette.

## Corpus de test (references/)

Signaux réels téléchargés du dépôt public `Zero-Sploit/FlipperZero-Subghz-DB` (13 717 signaux).

| fichier | protocole | bits | type |
|---|---|---|---|
| princeton.sub | Princeton | 24 | décodé |
| avidsen_carillon.sub | Princeton | 24 | décodé (carillon français) |
| came_portail.sub | CAME | 24 | décodé (portail français) |
| somfy_stores.sub | Somfy Telis | 56 | décodé (stores français) |
| bubendorff_down.sub | RAW | — | timings bruts (volet français) |
| bubendorff_up.sub | RAW | — | timings bruts |

### Équivalence Flipper -> rc-switch

```
Protocol: Princeton   ->  mySwitch.setProtocol(1);
Bit: 24               ->  mySwitch.send(valeur, 24);
Key: ...EF 38 38      ->  la valeur décimale du code
TE: 400               ->  mySwitch.setPulseLength(400);
Frequency: 433920000  ->  la bande 433,92 MHz
```

Pour les fichiers **RAW**, on ne décode pas : on rejoue la suite de durées telle quelle,
en microsecondes, avec `delayMicroseconds()`.

### Ligne de conduite

Les dépôts publics contiennent des dossiers `brute_force` / `Bruteforcing` — des milliers de
codes générés pour ouvrir des portails qui ne sont pas les siens. **Ce projet ne les utilise
pas.** Il analyse et rejoue **ses propres** appareils. C'est un analyseur, pas un passe-partout.

## Étapes

1. **Test de la carte** — croquis minimal, vérifier qu'elle démarre et parle.
2. **Test du récepteur seul** — écoute et affiche les timings bruts sur le port série.
   Valide le module et le câblage **avant** toute page web.
3. **Boucle fermée** — l'émetteur envoie une valeur connue (`avidsen_carillon.sub`),
   le récepteur la capte, les valeurs doivent correspondre. Validation complète de la chaîne.
4. **Décodage** avec `rc-switch` : reconnaître protocole, code et bits.
5. **Serveur WiFi français** : page, liste des signaux, noms, boutons de rejeu.
6. **Capture brutes** pour les protocoles non reconnus (Bubendorff, Somfy).

## Journal

- 2026-11-01 : projet créé, corpus de test téléchargé (6 signaux), câblage défini.
- 2026-11-02 : carte neuve validée (MAC 20:50:0D:33:C8:E8, COM10) ; chaîne 433 prouvée
  (porteuse 91 %/0 %) ; sonette confirmée 433 MHz ; moniteur temps réel + identification
  construits (`moniteur_temps_reel.py` sur http://localhost:8070, pyserial dans `.venv`).
- 2026-11-03 : **pivot vers un transceiver CC1101 + antenne SMA** (remplace émetteur +
  récepteur, 3,3 V sans diviseur, pas de soudure d'antenne). RadioLib installé (7.8.1).
  Câblage SPI prévu : VCC=3V3, SI=GPIO23, SO=GPIO19, SCLK=GPIO18, CSN=GPIO5, GDO0=GPIO4.
- 2026-11-03 (suite) : **serveur WiFi + téléphone** (`etape6_wifi`) : AP « Radar433 »,
  capture par interruption, catalogue (5 signaux RAW rejouables : Bubendorff, LiftMaster,
  AVANTEK, 1byone) + références d'identification (7 protocoles : Chacon, Nice FLO, CAME
  TWEE, Somfy, Princeton, Holtek). Décodeur brut (extraction bits + hexa + rapprochement).
  Flashé en `huge_app` (3 Mo, 29 % occupé).
  Moniteur PC : `moniteur_temps_reel.py` (http://localhost:8070, pyserial dans `.venv`).
- 2026-11-04 : **CC1101 intégré** (`etape7_cc1101/`). SPI : CSN=5, GDO0=4, SCK=18, MISO=19, MOSI=23.
  Module PCB bleu V2.0, connecteur **2×4** : 1=GND, 2=VCC, 3=GDO0, 4=CSN, 5=SCK, 6=MOSI, 7=MISO, 8=GDO2.
  Mode **OOK asynchrone** (GDO0 sort le bitstream démodulé) → même pipeline de capture qu'avant.
  Vumètre = **vrai RSSI dBm** (−90 → 0 %, −30 → 100 %). Rejeu via TX asynchrone du CC1101.
  **Filtre anti-bruit renforcé** (le CC1101 sort du bruit en continu) : ≥16 impulsions,
  période 80–1500 µs, ≥65 % des durées cohérentes, trame ≥5 ms. Résultat : 1137 → 10 trames/30 s.
  Vus en vrai : Holtek, Chacon, CAME TWEE, Somfy Telis. Décodeur encore heuristique (à remplacer par rtl_433_ESP).
  Compilé 1 002 115 o (31 %), RAM 27 %. Flashé COM9.
- 2026-11-03 (fin) : identité visuelle (bannière « SNIFFER RAUS » + monogramme SLCA, servis
  en `/logo` et `/slca` depuis des tableaux flash `logos.h`) ; **vumètre de porteuse en direct**
  (échantillonnage GPIO4, attaque/relâchement, affiché en barre segmentée). `rtl_433_ESP`
  cloné dans `rtl433_esp/` (223 décodeurs) pour intégration demain avec le CC1101.
  Snapshot : `sauvegardes/snapshot_YYYY-MM-DD_HHMM/`.

## À faire demain (CC1101 reçu)

1. Vérifier les étiquettes exactes des broches du module (photo à l'arrivée).
2. Câbler en SPI (3,3 V) : VCC/3V3, GND, SI/GPIO23, SO/GPIO19, SCLK/GPIO18, CSN/GPIO5, GDO0/GPIO4.
3. Brancher la **capture en RMT** (précision matérielle) + le **vrai RSSI** (vumètre en dBm).
4. Intégrer **rtl_433_ESP** (223 protocoles OOK/FSK) à la place de l'heuristique hexa.

## Note CC1101 (à finaliser à la réception)

- C'est un émetteur-récepteur 433 MHz piloté en SPI, alimenté en 3,3 V.
- Réception OOK/ASK pour sniffer la sonette : via RadioLib, plus sensible que le MX-RM-5V.
- L'antenne se visse (SMA) — aucune soudure.
- Vérifier les étiquettes exactes des broches sur le module à l'arrivée (SI/SO/SCLK
  ou MOSI/MISO/SCK selon les fabricants).

### Correction vumètre (2026-11-04)
Le **RSSI du CC1101 est figé** (registre 0x34 bloqué à 31) en mode OOK asynchrone — il ne
sert à rien pour un vumètre. Dégeler l'AGC (AGCCTRL0) fait pire : le bruit grimpe à
~4 600 fronts/s et sature tout. **Solution retenue** : le vumètre est déclenché par
**chaque trame réellement reconnue** (passe le filtre) → bond à 100 % puis décroissance
~0,8 s. Au repos il est à 0, et il monte franchement à chaque signal. UI rafraîchie à 250 ms.

### BUG CORRIGÉ — la page n'affichait plus les nouvelles trames (2026-11-04)
Les ids de trame étaient l'**index dans le tableau** (`i+1`), donc la plus récente avait
toujours l'id 1. La page ne rajoutait que `f.id > dernier` → après le 1er rafraîchissement,
**plus aucune trame ne s'affichait**, alors que la carte les captait (vérifié en série).
Fix : **id monotone** (`numeroTrame++`) stocké dans la struct, affichage trié par id,
et `Cache-Control: no-store` pour éviter les pages périmées après reflash.

### 2026-11-04 (fin) — révélation par la doc SX1278 de Thierry
Thierry avait déjà un sniffer OOK avec un **module LoRa SX1278** (RadioLib `transmitDirect`/`receiveDirect`),
qui captait ET rejouait sa sonnette. Leçons appliquées au CC1101 :
1. **Alignement de phase** : l'ISR ne commence l'enregistrement qu'à la FIN d'un état HAUT
   (`digitalRead(PIN_RX)==LOW` au 1er segment). Sinon la polarité du rejeu est inversée
   (ça marchait 1 fois sur 4 : 61 imp oui, 104/49/65 non).
2. **Anti-concaténation** : ECART_TRAME 8ms->4ms + finalisation si nbPulses>=190.
3. Rejeu avec pause inter-trame (delay 30ms) conservée.
Résultat : codes non-nuls (`0x0541240C`, `0x041A29AA0D14D5068A6A8345`) au lieu de 0x0000.
La télécommande (EV1527, pile 12V/23A, PCB "P2K") est confirmée **433 MHz**.
