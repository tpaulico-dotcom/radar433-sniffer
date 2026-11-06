/*
  RADAR 433 — SERVEUR WIFI + LECTURE EN DIRECT (étape 6)
  ------------------------------------------------------
  L'ESP32 crée son propre réseau WiFi "Radar433". Ton téléphone s'y connecte,
  ouvre http://192.168.4.1, et voit les signaux 433 MHz captés en direct.

  - Point d'accès WiFi (AP) : SSID "Radar433", sans mot de passe.
  - Capture NON bloquante : interruption sur GPIO 4, on enregistre les durées
    des impulsions sans jamais bloquer le serveur web.
  - Filtre anti-bruit : toute impulsion < 30 µs est ignorée.
  - Identification légère : période de bit estimée -> famille du protocole.

  Câblage actuel (récepteur simple) :
      récepteur DATA -> GPIO 4

  (Demain, on branchera le CC1101 en SPI à la place et on changera la partie
   capture uniquement — le serveur web et l'interface ne bougeront pas.)
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "catalogue.h"
#include "references.h"
#include "logos.h"

const int   PIN_RX        = 4;
const int   PIN_TX        = 5;
const char* SSID          = "Radar433";
const char* MOT_DE_PASSE  = "";             // réseau ouvert (à durcir plus tard)

const unsigned int SEUIL_BRUIT = 30;        // µs : en dessous, friture
const unsigned int SEUIL_TRAME = 80;        // µs : une trame doit dépasser ça
const unsigned long ECART_TRAME = 8000;     // 8 ms de silence = fin de trame

#define MAX_PULSES 200
#define MAX_TRAMES 40

// ---- Structure d'une trame (déclarée en haut : sinon le préprocesseur
// Arduino génère le prototype d'analyser avant la définition du type) ---------
struct Trame {
  unsigned int durees[MAX_PULSES];
  int nb;
  unsigned long total;
  unsigned int bitPeriod;
  char classe[48];
  char valeur[40];
};

// ---- Capture par interruption (non bloquante) ------------------------------
volatile uint32_t tPrec = 0;
volatile int      nbPulses = 0;
volatile unsigned int tampon[MAX_PULSES];

void IRAM_ATTR surChangement() {
  uint32_t t = micros();
  if (tPrec != 0 && nbPulses < MAX_PULSES) {
    tampon[nbPulses++] = (unsigned int)(t - tPrec);
  }
  tPrec = t;
}

// ---- Trames finalisées -----------------------------------------------------
Trame trames[MAX_TRAMES];
int nbTrames = 0;
unsigned long totalBrut = 0;

// vumètre : échantillonnage de la porteuse en direct
unsigned long echTotal = 0, echHauts = 0;
int niveau = 0;                  // 0..100

WebServer serveur(80);

// ---- Analyse d'une trame ----------------------------------------------------
void analyser(Trame &t) {
  t.valeur[0] = 0;
  t.classe[0] = 0;
  if (t.nb == 0) return;

  // médiane filtrée : période de bit
  unsigned int tri[MAX_PULSES];
  int n = 0;
  for (int i = 0; i < t.nb; i++)
    if (t.durees[i] >= SEUIL_BRUIT) tri[n++] = t.durees[i];
  unsigned int bp = 0;
  if (n > 0) {
    for (int i = 1; i < n; i++) {
      unsigned int v = tri[i]; int j = i - 1;
      while (j >= 0 && tri[j] > v) { tri[j + 1] = tri[j]; j--; }
      tri[j + 1] = v;
    }
    bp = tri[n / 2];
  }
  t.bitPeriod = bp;

  // Décodeur brut : marque courte (<1.7T) -> 0, moyenne (<4T) -> 1, longue -> sync.
  // Reconstruit la valeur hexadécimale par groupes de 4 bits.
  char seq[200];
  int bits = 0;
  if (bp > 0) {
    for (int i = 0; i < t.nb; i += 2) {
      int d = t.durees[i];
      if (d < SEUIL_BRUIT) continue;
      if (d < (int)(bp * 1.7f))        { if (bits < 190) seq[bits++] = '0'; }
      else if (d < (int)(bp * 4.0f))   { if (bits < 190) seq[bits++] = '1'; }
    }
    int k = 0;
    for (int i = 0; i < bits && k < 36; i += 4) {
      int v = 0;
      for (int j = 0; j < 4; j++) { v <<= 1; if (i + j < bits && seq[i + j] == '1') v |= 1; }
      t.valeur[k++] = "0123456789ABCDEF"[v];
    }
    t.valeur[k] = 0;
  }

  // Identification contre les références (par nombre d'impulsions).
  int bitsEstimes = n / 2;
  const char* meilleur = nullptr;
  int ecartMin = 1000;
  for (int i = 0; i < NB_REFERENCES; i++) {
    int ecart = abs(bitsEstimes - REFERENCES[i].bits);
    if (ecart < ecartMin) { ecartMin = ecart; meilleur = REFERENCES[i].nom; }
  }
  if (meilleur && ecartMin <= 8) {
    strcpy(t.classe, meilleur);
  } else if (bits >= 16) {
    snprintf(t.classe, 48, "code ~%d bits", bits);
  } else {
    strcpy(t.classe, "signal court / inconnu");
  }
}

// ---- Finaliser la trame en cours (appelé dans loop) ------------------------
void finaliserTrame() {
  int n = nbPulses;
  if (n < 4) { nbPulses = 0; tPrec = 0; return; }

  Trame t;
  t.nb = 0;
  t.total = 0;
  for (int i = 0; i < n && i < MAX_PULSES; i++) {
    t.durees[t.nb] = tampon[i];
    t.total += tampon[i];
    t.nb++;
  }

  // filtre : la trame doit dépasser le seuil
  unsigned int mx = 0;
  for (int i = 0; i < t.nb; i++) if (t.durees[i] > mx) mx = t.durees[i];
  totalBrut++;
  nbPulses = 0;
  tPrec = 0;
  if (mx < SEUIL_TRAME) return;    // friture

  analyser(t);
  if (t.nb < 4) return;

  // rangement (décalage vers le haut)
  if (nbTrames < MAX_TRAMES) nbTrames++;
  for (int i = nbTrames - 1; i > 0; i--) trames[i] = trames[i - 1];
  trames[0] = t;
}

// ---- Émission brute (bit-bang) ---------------------------------------------
// Rejoue une séquence de durées en µs : impulsions paires = porteuse, impaires
// = silence. 3 répétitions, comme une vraie télécommande.
void envoyerRaw(const uint16_t* seq, int n) {
  for (int rep = 0; rep < 3; rep++) {
    for (int i = 0; i < n; i++) {
      digitalWrite(PIN_TX, (i % 2 == 0) ? HIGH : LOW);
      delayMicroseconds(seq[i]);
    }
    digitalWrite(PIN_TX, LOW);
    delay(30);
  }
}

// ---- Catalogue utilisateur (captures nommées, en NVS) ----------------------
#define MAX_CAPTURES_UTIL 12
struct CaptureUtilisateur {
  char nom[28];
  uint16_t seq[200];
  int nb;
};
CaptureUtilisateur captU[MAX_CAPTURES_UTIL];
int nbCaptU = 0;

Preferences prefs;

void chargerCaptures() {
  nbCaptU = prefs.getInt("nb", 0);
  if (nbCaptU > MAX_CAPTURES_UTIL) nbCaptU = MAX_CAPTURES_UTIL;
  for (int i = 0; i < nbCaptU; i++) {
    String n = prefs.getString(("n" + String(i)).c_str(), "");
    n.toCharArray(captU[i].nom, 28);
    String s = prefs.getString(("s" + String(i)).c_str(), "");
    int nb = 0, debut = 0;
    while (nb < 200) {
      int virg = s.indexOf(',', debut);
      String v = (virg < 0) ? s.substring(debut) : s.substring(debut, virg);
      if (v.length() == 0) break;
      captU[i].seq[nb++] = v.toInt();
      if (virg < 0) break;
      debut = virg + 1;
    }
    captU[i].nb = nb;
  }
}

void sauverDerniereCapture(const char* nom) {
  if (nbTrames == 0 || nom == nullptr || nom[0] == 0) return;
  if (nbCaptU >= MAX_CAPTURES_UTIL) return;
  int i = nbCaptU;
  strncpy(captU[i].nom, nom, 27); captU[i].nom[27] = 0;
  int n = trames[0].nb < 200 ? trames[0].nb : 200;
  captU[i].nb = n;
  for (int k = 0; k < n; k++) captU[i].seq[k] = trames[0].durees[k];
  nbCaptU++;
  prefs.putInt("nb", nbCaptU);
  prefs.putString(("n" + String(i)).c_str(), String(captU[i].nom));
  String s = "";
  for (int k = 0; k < n; k++) { if (k) s += ","; s += String(captU[i].seq[k]); }
  prefs.putString(("s" + String(i)).c_str(), s);
}

// ---- Réponses du serveur ---------------------------------------------------
void gererCatalogue() {
  String json = "{\"catalogue\":[";
  int total = NB_CATALOGUE + nbCaptU;
  for (int i = 0; i < NB_CATALOGUE; i++) {
    json += "{\"id\":" + String(i) + ",\"nom\":\"" + String(CATALOGUE[i].nom) + "\"}";
    if (i < total - 1) json += ",";
  }
  for (int i = 0; i < nbCaptU; i++) {
    json += "{\"id\":" + String(NB_CATALOGUE + i) + ",\"nom\":\"" + String(captU[i].nom) + "\"}";
    if (i < nbCaptU - 1) json += ",";
  }
  json += "]}";
  serveur.send(200, "application/json", json);
}

void gererEnvoyer() {
  int id = serveur.arg("id").toInt();
  if (id >= 0 && id < NB_CATALOGUE) {
    envoyerRaw(CATALOGUE[id].seq, CATALOGUE[id].nb);
    serveur.send(200, "application/json", "{\"ok\":true,\"nom\":\"" + String(CATALOGUE[id].nom) + "\"}");
  } else if (id >= NB_CATALOGUE && id < NB_CATALOGUE + nbCaptU) {
    int i = id - NB_CATALOGUE;
    envoyerRaw(captU[i].seq, captU[i].nb);
    serveur.send(200, "application/json", "{\"ok\":true,\"nom\":\"" + String(captU[i].nom) + "\"}");
  } else {
    serveur.send(400, "application/json", "{\"ok\":false}");
  }
}

void gererEnregistrer() {
  sauverDerniereCapture(serveur.arg("nom").c_str());
  serveur.send(200, "application/json", "{\"ok\":true}");
}

// ---- Page web --------------------------------------------------------------
const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1">
<title>Radar 433</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:#0b0f14;color:#c9d4de;font-family:-apple-system,'Segoe UI',Roboto,sans-serif;
    padding:14px 12px 40px;font-size:15px}
  h1{font-size:19px;color:#87CEEB;font-weight:600;margin-bottom:2px}
  .sous{font-size:12px;color:#7d8a97;margin-bottom:12px}
  .stats{display:flex;gap:18px;margin:4px 0 12px}
  .stats b{color:#87CEEB;font-size:22px;font-weight:600}
  .stats .t{color:#7d8a97;font-size:11px;display:block}
  .leg{display:flex;gap:14px;font-size:11.5px;color:#7d8a97;margin-bottom:12px;flex-wrap:wrap}
  .leg span i{width:12px;height:12px;display:inline-block;border-radius:3px;margin-right:5px;vertical-align:-2px}
  .ligne{margin-bottom:11px}
  .entete{font-size:11.5px;color:#7d8a97;margin-bottom:2px}
  .entete .classe{color:#ffd166;font-weight:600}
  .entete .val{color:#5be08a;font-weight:600;font-family:monospace;font-size:13px}
  svg{background:#0b0f14;border:1px solid #1c2631;border-radius:4px;display:block;width:100%}
  .vide{color:#7d8a97;font-size:13px;margin-top:24px;text-align:center}
  .led{width:9px;height:9px;border-radius:50%;background:#3a4652;display:inline-block;margin-right:6px}
  .led.on{background:#5be08a;box-shadow:0 0 8px #5be08a}
  .nav{position:sticky;top:0;background:#0b0f14;padding:8px 0 6px;border-bottom:1px solid #1c2631;margin-bottom:10px}
  .titreb{display:flex;align-items:flex-start;justify-content:space-between;gap:8px;margin-bottom:6px}
  .logo{max-width:80%;height:auto;display:block;border-radius:4px}
  .mini{width:38px;height:38px;object-fit:contain;flex-shrink:0}
  .vuwrap{position:relative;height:16px;background:#111820;border:1px solid #1c2631;border-radius:8px;overflow:hidden;margin:4px 0 2px}
  .vufill{height:100%;width:0%;background:linear-gradient(90deg,#5be08a 0%,#ffd166 55%,#ff5b5b 88%);transition:width .16s ease}
  .vu_ticks{position:absolute;inset:0;background:repeating-linear-gradient(90deg,transparent 0 24px,rgba(0,0,0,.4) 24px 25px);pointer-events:none}
  .vulabel{position:absolute;top:0;right:5px;font-size:10px;color:#0b0f14;line-height:16px;font-weight:600;pointer-events:none}
  .vulegende{display:flex;justify-content:space-between;font-size:10px;color:#7d8a97;margin-bottom:10px}
  .sec{font-size:14px;color:#87CEEB;font-weight:600;margin:20px 0 8px;border-top:1px solid #1c2631;padding-top:14px}
  .cat{display:flex;justify-content:space-between;align-items:center;gap:10px;
    padding:10px 12px;border:1px solid #1c2631;border-radius:6px;margin-bottom:7px;font-size:14px}
  .cat button,.save button{background:#87CEEB;color:#0b0f14;border:none;border-radius:5px;
    padding:8px 14px;font-size:14px;font-weight:600;font-family:inherit}
  .cat button:active,.save button:active{opacity:.7}
  .save{display:flex;gap:8px}
  .save input{flex:1;background:#111820;color:#c9d4de;border:1px solid #1c2631;
    border-radius:6px;padding:9px 11px;font-size:14px;font-family:inherit;min-width:0}
</style></head><body>
<div class="nav">
  <div class="titreb"><img class="logo" src="/logo" alt="SNIFFER RAUS"><img class="mini" src="/slca" alt="SLCA"></div>
  <div class="vuwrap"><div class="vufill" id="vufill"></div><div class="vu_ticks"></div><span class="vulabel" id="vulabel">0%</span></div>
  <div class="vulegende"><span>repos</span><span>signal 433 MHz</span><span>max</span></div>
  <div class="sous"><span class="led" id="led"></span><span id="statut">connexion…</span></div>
  <div class="stats">
    <div><b id="c_trames">0</b><span class="t">trames</span></div>
    <div><b id="c_dernier">—</b><span class="t">dernier</span></div>
    <div><b id="c_brutes">0</b><span class="t">brutes</span></div>
  </div>
  <div class="leg"><span><i style="background:#87CEEB"></i>porteuse</span>
    <span><i style="background:#1b232e;border:1px solid #33404d"></i>silence</span>
    <span><i style="color:#ffd166">■</i> identification</span></div>
</div>
<div id="flux"><div class="vide">Attente d'un signal 433 MHz…</div></div>

<h2 class="sec">Catalogue — envoyer un signal</h2>
<div id="catalogue" class="catalogue">chargement…</div>

<h2 class="sec">Capturer + nommer</h2>
<div class="save">
  <input type="text" id="nom" placeholder="nom (ex : portail)">
  <button id="btnSave">Enregistrer la dernière capture</button>
</div>

<script>
let dernier = 0;
function dessiner(d) {
  const t = d.reduce((a,b)=>a+b,0)||1, ech=1000/t;
  let s='',x=0;
  d.forEach((v,i)=>{const w=v*ech;const c=(i%2===0)?'#87CEEB':'#1b232e';
    s+=`<rect x="${x.toFixed(1)}" y="2" width="${w.toFixed(1)}" height="30" fill="${c}"><title>${v} µs</title></rect>`;x+=w;});
  return `<svg viewBox="0 0 1000 34"><rect width="1000" height="34" fill="#0b0f14"/>${s}</svg>`;
}
function ligne(f){
  const el=document.createElement('div');el.className='ligne';
  el.innerHTML=`<div class="entete"><span class="classe">${f.classe||'…'}</span>
    ${f.valeur ? '<span class="val">0x' + f.valeur + '</span>' : ''} ${f.nb} imp · ~${f.bp} µs/bit</div>${dessiner(f.d)}`;
  document.getElementById('flux').prepend(el);
  while(document.getElementById('flux').children.length>25)
    document.getElementById('flux').lastChild.remove();
}
async function tick(){
  try{
    const r=await fetch('/data');const j=await r.json();
      const v=Math.max(0,Math.min(100,j.niveau||0));
      document.getElementById('vufill').style.width=v+'%';
      document.getElementById('vulabel').textContent=v+'%';
    document.getElementById('c_trames').textContent=j.trames;
    document.getElementById('c_brutes').textContent=j.brutes;
    document.getElementById('statut').textContent=j.statut;
    document.getElementById('led').className='led'+(j.actif?' on':'');
    if(j.frames.length){
      document.getElementById('c_dernier').textContent='#'+j.frames[0].id;
      for(const f of j.frames){ if(f.id>dernier){ligne(f);dernier=f.id;} }
      const v=document.querySelector('.vide');if(v)v.remove();
    }
  }catch(e){}
}
async function chargerCatalogue(){
  try{
    const r=await fetch('/catalogue');const j=await r.json();
    const c=document.getElementById('catalogue');
    if(!j.catalogue.length){c.innerHTML='<div class="vide">catalogue vide</div>';return;}
    c.innerHTML=j.catalogue.map(e=>
      `<div class="cat"><span>${e.nom}</span><button data-id="${e.id}">Envoyer</button></div>`).join('');
    c.querySelectorAll('button').forEach(b=>b.onclick=async()=>{
      b.textContent='…'; await fetch('/envoyer?id='+b.dataset.id); setTimeout(()=>b.textContent='Envoyer',600);
    });
  }catch(e){}
}
document.getElementById('btnSave').addEventListener('click',async()=>{
  const n=document.getElementById('nom').value.trim();
  if(!n)return;
  await fetch('/enregistrer?nom='+encodeURIComponent(n));
  document.getElementById('nom').value='';
  chargerCatalogue();
});
setInterval(tick,500);tick();
chargerCatalogue();
</script></body></html>
)rawliteral";

// ---- Routes ----------------------------------------------------------------
void servir_logo_raus() {
  serveur.send_P(200, "image/jpeg", (const char*)LOGO_RAUS, LOGO_RAUS_LEN);
}
void servir_logo_slca() {
  serveur.send_P(200, "image/png", (const char*)LOGO_SLCA, LOGO_SLCA_LEN);
}

void gererData() {
  String json = "{\"niveau\":" + String(niveau) + ",\"trames\":" + String(nbTrames) +
                ",\"brutes\":" + String(totalBrut) +
                ",\"actif\":" + String(micros() - tPrec < ECART_TRAME ? "true" : "false") +
                ",\"statut\":\"émission en direct\""
                ",\"frames\":[";
  for (int i = 0; i < nbTrames; i++) {
    Trame &t = trames[i];
    json += "{\"id\":" + String(i + 1) +
            ",\"nb\":" + String(t.nb) +
            ",\"total\":" + String(t.total) +
            ",\"bp\":" + String(t.bitPeriod) + ",\"valeur\":\"" + String(t.valeur) + "\"" +
            ",\"classe\":\"" + String(t.classe) + "\"" +
            ",\"d\":[";
    int nbEnvoi = t.nb < 120 ? t.nb : 120;
    for (int k = 0; k < nbEnvoi; k++) {
      if (k) json += ",";
      json += String(t.durees[k]);
    }
    json += "]}";
    if (i < nbTrames - 1) json += ",";
  }
  json += "]}";
  serveur.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(SSID, MOT_DE_PASSE);

  pinMode(PIN_RX, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_RX), surChangement, CHANGE);

  pinMode(PIN_TX, OUTPUT);
  digitalWrite(PIN_TX, LOW);

  prefs.begin("radar433", false);
  chargerCaptures();

  serveur.on("/", []() { serveur.send(200, "text/html", PAGE); });
  serveur.on("/data", gererData);
  serveur.on("/catalogue", gererCatalogue);
  serveur.on("/envoyer", gererEnvoyer);
  serveur.on("/enregistrer", gererEnregistrer);
  serveur.on("/logo", servir_logo_raus);
  serveur.on("/slca", servir_logo_slca);
  serveur.begin();

  Serial.println();
  Serial.print("Réseau WiFi : ");
  Serial.println(SSID);
  Serial.println("Adresse : http://192.168.4.1");
}

void loop() {
  // échantillonnage du niveau de porteuse (~10 kHz)
  static uint32_t tEch = 0;
  if (micros() - tEch >= 100) {
    tEch = micros();
    echTotal++;
    if (digitalRead(PIN_RX)) echHauts++;
  }

  // calcul du niveau toutes les 250 ms : attaque rapide, relâchement lent
  static unsigned long tMetre = 0;
  if (millis() - tMetre >= 250) {
    tMetre = millis();
    int instantane = (echTotal > 100) ? (int)(echHauts * 100 / echTotal) : 0;
    echTotal = echHauts = 0;
    if (instantane > niveau) niveau = instantane;
    else niveau = (niveau * 2 + instantane) / 3;
    if (niveau < 2) niveau = 0;
  }

  // finaliser la trame en cours si le signal s'est arrêté
  if (nbPulses > 0 && tPrec != 0 && (micros() - tPrec) > ECART_TRAME) {
    finaliserTrame();
  }
  serveur.handleClient();
}