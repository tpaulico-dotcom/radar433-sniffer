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

  Matériel : CC1101 en SPI, mode OOK ASYNCHRONE (sniffer brut).
      CSN  -> GPIO 5      GDO0 -> GPIO 4
      SCK  -> GPIO 18     MISO -> GPIO 19     MOSI -> GPIO 23
      GDO0 sort le bitstream démodulé (comme un récepteur, mais radio pro).
      Le RSSI du CC1101 alimente le vumètre en vrais dBm.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SPI.h>
#include <RadioLib.h>
#include "catalogue.h"
#include "references.h"
#include "logos.h"

const int   PIN_RX        = 4;    // GDO0 du CC1101 (data démodulée)
const int   PIN_CS        = 5;    // CSN du CC1101
const int   PIN_SCK       = 18;
const int   PIN_MISO      = 19;
const int   PIN_MOSI      = 23;
const char* SSID          = "Radar433";
const char* MOT_DE_PASSE  = "";             // réseau ouvert (à durcir plus tard)

const unsigned int SEUIL_BRUIT = 80;        // µs : en dessous, friture
const unsigned int SEUIL_TRAME = 80;        // µs : une trame doit dépasser ça
const unsigned long ECART_TRAME = 8000;     // 8 ms de silence = fin de trame

#define MAX_PULSES 200
#define MAX_TRAMES 40

// ---- Structure d'une trame (déclarée en haut : sinon le préprocesseur
// Arduino génère le prototype d'analyser avant la définition du type) ---------
struct Trame {
  unsigned int durees[MAX_PULSES];
  unsigned long id;          // numéro unique croissant (jamais réutilisé)
  int nb;
  unsigned long total;
  unsigned int bitPeriod;
  char classe[48];
  char valeur[40];
};

// ---- Capture par interruption (non bloquante) ------------------------------
volatile uint32_t tPrec = 0;
volatile int      nbPulses = 0;
volatile uint32_t fronts = 0;          // nombre de fronts (activité du signal)
volatile unsigned int tampon[MAX_PULSES];
volatile bool captureActive = false;   // mode capture ON/OFF (defaut : pause)

void IRAM_ATTR surChangement() {
  if (!captureActive) { tPrec = 0; return; }   // capture en pause : on ignore
  uint32_t t = micros();
  if (tPrec != 0 && nbPulses < MAX_PULSES) {
    uint32_t d = t - tPrec;
    if (nbPulses == 0) {
      // ALIGNEMENT DE PHASE : n'accepter le 1er segment qu'à la fin d'un état
      // HAUT (front tombant -> la pin est BAS). Sinon la polarité du rejeu
      // est inversée et le récepteur cible rejette le signal.
      if (digitalRead(PIN_RX) == LOW) {
        tampon[nbPulses++] = (unsigned int)d;
      }
    } else {
      tampon[nbPulses++] = (unsigned int)d;
    }
  }
  tPrec = t;
  fronts++;
}

// ---- Trames finalisées -----------------------------------------------------
Trame trames[MAX_TRAMES];
int nbTrames = 0;
unsigned long totalBrut = 0;
unsigned long numeroTrame = 0;   // id monotone

// vumètre : échantillonnage de la porteuse en direct
unsigned long echTotal = 0, echHauts = 0;
int niveau = 0;                  // 0..100
float picNiv = 0.0f;             // crête du vumètre (déclenchée par une trame)

WebServer serveur(80);

// ---- CC1101 -----------------------------------------------------------------
CC1101 radio = new Module(PIN_CS, PIN_RX, RADIOLIB_NC, RADIOLIB_NC);
bool  radioOk   = false;
float rssiActuel = -100.0f;

// spectrogramme : historique du RSSI (dBm entier), echantillonne a 25 ms
#define HISTO_LEN 512
int histoDb[HISTO_LEN];
int histoIdx = 0;
unsigned long hcount = 0;

// --- Accès SPI directs au CC1101 (les méthodes RadioLib sont protected) ------
SPISettings ccSpi(4000000, MSBFIRST, SPI_MODE0);

void ccEcrit(uint8_t reg, uint8_t val) {
  SPI.beginTransaction(ccSpi);
  digitalWrite(PIN_CS, LOW);
  delayMicroseconds(2);
  SPI.transfer(reg & 0x3F);          // écriture : bit7=0
  SPI.transfer(val);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();
}

uint8_t ccLit(uint8_t reg) {
  uint8_t hdr = reg | 0x80;                       // bit7 = lecture
  if (reg >= 0x30 && reg <= 0x3D) hdr |= 0x40;    // registres de statut : bit burst à 1
  SPI.beginTransaction(ccSpi);
  digitalWrite(PIN_CS, LOW);
  delayMicroseconds(2);
  SPI.transfer(hdr);
  uint8_t v = SPI.transfer(0);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();
  return v;
}

void ccCommande(uint8_t cmd) {
  SPI.beginTransaction(ccSpi);
  digitalWrite(PIN_CS, LOW);
  delayMicroseconds(2);
  SPI.transfer(cmd);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();
  delayMicroseconds(2);
}

// OOK asynchrone en réception : GDO0 = bitstream démodulé, sans préambule/sync/CRC
void configurerRXAsync() {
  ccEcrit(RADIOLIB_CC1101_REG_PKTCTRL0, 0x32);  // mode série asynchrone
  ccEcrit(RADIOLIB_CC1101_REG_IOCFG0,   0x0D);  // GDO0 = sortie data RX
  ccEcrit(RADIOLIB_CC1101_REG_MCSM1,    0x0F);  // reste en RX
  ccCommande(RADIOLIB_CC1101_CMD_RX);
}

// ---- Analyse d'une trame ----------------------------------------------------
void analyser(Trame &t) {
  t.valeur[0] = 0;
  t.classe[0] = 0;
  if (t.nb < 4) return;

  // 1. Durée "unité" = médiane des durées filtrées (< 2 ms -> hors sync)
  unsigned int tri[MAX_PULSES];
  int n = 0;
  for (int i = 0; i < t.nb; i++)
    if (t.durees[i] >= SEUIL_BRUIT && t.durees[i] < 2000) tri[n++] = t.durees[i];
  if (n < 8) return;
  for (int i = 1; i < n; i++) {
    unsigned int v = tri[i]; int j = i - 1;
    while (j >= 0 && tri[j] > v) { tri[j + 1] = tri[j]; j--; }
    tri[j + 1] = v;
  }
  unsigned int unit = tri[n / 2];
  t.bitPeriod = unit;

  // 2. Décodage différentiel PWM : pour chaque paire (marque, espace),
  //    marque nettement > espace => 1, espace nettement > marque => 0.
  char seq[200];
  int bits = 0;
  for (int i = 0; i + 1 < t.nb; i += 2) {
    unsigned int mark  = t.durees[i];
    unsigned int space = t.durees[i + 1];
    if (mark < SEUIL_BRUIT || space < SEUIL_BRUIT) continue;
    if (mark > unit * 6 || space > unit * 6) continue;  // sync / gap long
    if (mark > space * 2)       { if (bits < 190) seq[bits++] = '1'; }
    else if (space > mark * 2)  { if (bits < 190) seq[bits++] = '0'; }
  }

  // 3. Hex (groupes de 4 bits)
  int k = 0;
  for (int i = 0; i < bits && k < 36; i += 4) {
    int v = 0;
    for (int j = 0; j < 4; j++) { v <<= 1; if (i + j < bits && seq[i + j] == '1') v |= 1; }
    t.valeur[k++] = "0123456789ABCDEF"[v];
  }
  t.valeur[k] = 0;

  // 4. Classe = nombre de bits décodés
  if (bits >= 12) snprintf(t.classe, 48, "code %d bits", bits);
  else strcpy(t.classe, "signal court / inconnu");
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

  // ---- FILTRE ANTI-BRUIT RENFORCÉ -------------------------------------------
  // Le CC1101 en OOK asynchrone sort du bruit en continu sans porteuse.
  // Un vrai signal a beaucoup d'impulsions, une période plausible et des
  // durées regroupées autour de cette période. Le bruit, lui, est dispersé.
  if (t.nb < 16) return;                               // vraie trame >= 16 impulsions
  if (t.bitPeriod < 80 || t.bitPeriod > 1500) return;  // période plausible
  int propres = 0;
  for (int i = 0; i < t.nb; i++) {
    float r = (float)t.durees[i] / (float)t.bitPeriod;
    if (r >= 0.5f && r <= 3.5f) propres++;
  }
  if (propres * 100 / t.nb < 65) return;               // durées cohérentes = vrai signal
  if (t.total < 5000UL) return;                        // trop court = friture

  picNiv = 100.0f;                       // le vumètre bondit sur signal réel
  Serial.printf("[TRAME] %d imp | %u us/bit | %s | 0x%s\n",
                t.nb, t.bitPeriod, t.classe, t.valeur);
  { int ndc = t.nb < 60 ? t.nb : 60;
    Serial.print("[DONNEES] ");
    for (int k = 0; k < ndc; k++) { Serial.print(t.durees[k]); Serial.print(","); }
    Serial.println(); }

  // rangement (décalage vers le haut)
  t.id = ++numeroTrame;
  if (nbTrames < MAX_TRAMES) nbTrames++;
  for (int i = nbTrames - 1; i > 0; i--) trames[i] = trames[i - 1];
  trames[0] = t;
}

// ---- Émission brute (bit-bang) ---------------------------------------------
// Rejoue une séquence de durées en µs : impulsions paires = porteuse, impaires
// = silence. 3 répétitions, comme une vraie télécommande.
void envoyerRaw(const uint16_t* seq, int n) {
  if (!radioOk || n <= 0) return;
  detachInterrupt(digitalPinToInterrupt(PIN_RX));   // ne pas capter notre propre émission
  ccCommande(RADIOLIB_CC1101_CMD_IDLE);
  ccEcrit(RADIOLIB_CC1101_REG_PKTCTRL0, 0x32);
  ccEcrit(RADIOLIB_CC1101_REG_IOCFG0,   0x2D);      // GDO0 = entrée data TX
  ccCommande(RADIOLIB_CC1101_CMD_TX);
  pinMode(PIN_RX, OUTPUT);
  for (int rep = 0; rep < 3; rep++) {
    for (int i = 0; i < n; i++) {
      digitalWrite(PIN_RX, (i % 2 == 0) ? HIGH : LOW);
      delayMicroseconds(seq[i]);
    }
    digitalWrite(PIN_RX, LOW);
    delay(30);
  }
  ccCommande(RADIOLIB_CC1101_CMD_IDLE);
  pinMode(PIN_RX, INPUT);
  ccEcrit(RADIOLIB_CC1101_REG_IOCFG0, 0x0D);        // retour en RX
  ccCommande(RADIOLIB_CC1101_CMD_RX);
  nbPulses = 0; tPrec = 0;
  attachInterrupt(digitalPinToInterrupt(PIN_RX), surChangement, CHANGE);
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

void sauverCapture(Trame &trame, const char* nom) {
  if (nom == nullptr || nom[0] == 0) return;
  if (nbCaptU >= MAX_CAPTURES_UTIL) return;
  int i = nbCaptU;
  strncpy(captU[i].nom, nom, 27); captU[i].nom[27] = 0;
  int n = trame.nb < 200 ? trame.nb : 200;
  captU[i].nb = n;
  for (int k = 0; k < n; k++) captU[i].seq[k] = trame.durees[k];
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
    json += "{\"id\":" + String(NB_CATALOGUE + i) + ",\"nom\":\"" + String(captU[i].nom) + "\",\"del\":true}";
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
  String sid = serveur.arg("id");
  Trame* src = nullptr;
  if (sid.length() > 0) {
    unsigned long id = sid.toInt();
    for (int i = 0; i < nbTrames; i++) if (trames[i].id == id) { src = &trames[i]; break; }
  } else if (nbTrames > 0) {
    src = &trames[0];
  }
  if (src) sauverCapture(*src, serveur.arg("nom").c_str());
  serveur.send(200, "application/json", "{\"ok\":true}");
}

void gererCapture() {
  captureActive = (serveur.arg("on").toInt() != 0);
  nbPulses = 0; tPrec = 0;
  Serial.printf("[CAPTURE] %s\n", captureActive ? "ON" : "OFF");
  serveur.send(200, "application/json", captureActive ? "{\"capture\":true}" : "{\"capture\":false}");
}

void gererSupprimer() {
  int id = serveur.arg("id").toInt();
  int i = id - NB_CATALOGUE;
  if (i < 0 || i >= nbCaptU) { serveur.send(400, "application/json", "{\"ok\":false}"); return; }
  for (int j = i; j < nbCaptU - 1; j++) captU[j] = captU[j + 1];
  nbCaptU--;
  prefs.putInt("nb", nbCaptU);
  for (int j = 0; j < nbCaptU; j++) {
    prefs.putString(("n" + String(j)).c_str(), String(captU[j].nom));
    String ss = "";
    for (int k = 0; k < captU[j].nb; k++) { if (k) ss += ","; ss += String(captU[j].seq[k]); }
    prefs.putString(("s" + String(j)).c_str(), ss);
  }
  serveur.send(200, "application/json", "{\"ok\":true}");
}

// ---- Page web --------------------------------------------------------------
const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="fr"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>SNIFFER RAUS · RF 433.92</title>
<style>
  :root{--bg:#000055;--panel:#0000a8;--border:#ffffff;--text:#ffffff;--dim:#aaaaaa;--accent:#ffff55;
    --hi:#00aaaa;--hitext:#000000;--btn:#00aaaa;--btntext:#000000;--danger:#ff5555;--ok:#55ff55;--scan:0.12}
  body.theme-amiga{--bg:#001830;--panel:#002347;--border:#7fb2f0;--text:#e0e8f0;--dim:#5a7a9a;--accent:#ffb000;
    --hi:#ff8000;--hitext:#001830;--btn:#a85700;--btntext:#001830;--danger:#ff6060;--ok:#50ff50;--scan:0.16}
  *{box-sizing:border-box;margin:0;padding:0;font-family:"Courier New",Courier,"Liberation Mono",monospace;
    -webkit-tap-highlight-color:transparent;touch-action:manipulation}
  body{background:var(--bg);color:var(--text);padding:6px;min-height:100vh;overflow-x:hidden}
  .crt{position:fixed;inset:0;background:linear-gradient(rgba(18,16,16,0) 50%,rgba(0,0,0,.4) 50%);background-size:100% 3px;z-index:99;pointer-events:none;opacity:var(--scan)}
  .app{max-width:1200px;margin:0 auto;display:flex;flex-direction:column;gap:8px;padding-bottom:56px}
  .dos-box{background:var(--panel);border:2px solid var(--border);position:relative;padding:10px 8px 8px;box-shadow:3px 3px 0 rgba(0,0,0,.6)}
  .dos-title{position:absolute;top:-11px;left:10px;background:var(--panel);padding:0 6px;color:var(--accent);font-weight:bold;font-size:11px;text-transform:uppercase;letter-spacing:.5px;white-space:nowrap}
  .hd{display:flex;justify-content:space-between;align-items:flex-start;gap:8px;padding-top:2px}
  .logos{display:flex;align-items:center;gap:7px;margin-bottom:3px}
  .logos .r{height:22px;width:auto;display:block}
  .logos .s{height:16px;width:auto;display:block;opacity:.95}
  .hd .sub{font-size:9px;color:var(--dim)}
  .hd .right{text-align:right;font-size:11px}
  .st-cap{color:var(--ok);font-weight:bold} .st-cap.off{color:var(--danger)}
  .st-rssi{color:var(--accent)}
  .themes{display:grid;grid-template-columns:1fr 1fr;gap:5px;margin-top:8px;padding-top:8px;border-top:1px solid var(--border)}
  .dos-btn{background:var(--btn);color:var(--btntext);border:1px solid var(--border);font-weight:bold;padding:10px 12px;min-height:44px;font-family:inherit;cursor:pointer;text-transform:uppercase;display:inline-flex;align-items:center;justify-content:center;gap:6px;box-shadow:2px 2px 0 #000;font-size:12px}
  .dos-btn:active{transform:translate(1px,1px);box-shadow:0 0 0 #000;background:var(--accent);color:#000}
  .dos-btn.sm{padding:5px 9px;min-height:34px;font-size:10.5px}
  .dos-btn.rec{background:transparent;color:var(--danger);border:1px solid var(--danger)}
  .tabs{display:flex;gap:4px}
  .tab-btn{flex:1;text-align:center;padding:9px 4px;font-size:11px;font-weight:bold;border:1px solid var(--border);background:var(--bg);color:var(--text);font-family:inherit}
  .tab-btn.active{background:var(--hi);color:var(--hitext)}
  .grid{display:flex;flex-direction:column;gap:8px}
  /* vumetre */
  .vu{position:relative;height:12px;background:var(--bg);border:1px solid var(--border);margin-top:4px}
  .vu .fill{height:100%;width:0%;background:linear-gradient(90deg,#55ff55,#ffff55 55%,#ff5555 88%);transition:width .15s}
  .vu .lbl{position:absolute;top:0;right:5px;font-size:9px;line-height:12px;color:var(--text);font-weight:bold;text-shadow:0 0 2px #000}
  .scope-wrap{position:relative;width:100%;height:200px;background:#000;border:1px solid var(--border);margin-top:6px}
  canvas#scope{position:absolute;inset:0;width:100%;height:100%;display:block}
  .sc-label{position:absolute;top:3px;left:6px;font-size:9px;color:var(--dim)}
  .sc-cut{position:absolute;bottom:3px;left:6px;font-size:9px;color:var(--danger)}
  .ctrls{display:flex;flex-direction:column;gap:7px;background:var(--bg);padding:8px;border:1px solid var(--border);margin-top:6px;font-size:11px}
  .ctl label{display:flex;justify-content:space-between;font-weight:bold;margin-bottom:3px}
  .ctl .v{color:var(--accent)}
  input[type=range]{-webkit-appearance:none;background:var(--bg);border:1px solid var(--border);height:18px;width:100%;border-radius:0}
  input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:24px;height:24px;background:var(--accent);border:2px solid var(--bg);cursor:pointer}
  input[type=text]{background:var(--bg);color:var(--text);border:1px solid var(--border);outline:none;padding:8px;min-height:36px;font-size:12px;width:100%;font-family:inherit}
  .acts{display:grid;grid-template-columns:1fr 1fr;gap:6px;margin-top:6px}
  .cat-list{display:flex;flex-direction:column;gap:6px;max-height:340px;overflow-y:auto;padding-right:2px}
  .cat-item{border:1px solid var(--border);background:var(--bg);padding:8px;display:flex;justify-content:space-between;align-items:center;gap:8px}
  .cat-item .nm{font-weight:bold;font-size:11px;color:var(--accent);word-break:break-all}
  .cat-item .btns{display:flex;gap:4px;flex-shrink:0}
  .hint{font-size:10px;color:var(--dim);margin-bottom:6px}
  .inject{margin-top:10px;padding-top:8px;border-top:1px solid var(--border);display:flex;flex-direction:column;gap:6px}
  .inject .lbl{font-size:11px;font-weight:bold;color:var(--accent)}
  .inject .row{display:flex;gap:5px}
  .inject input{flex:1}
  /* bandes de trames */
  .frames{display:flex;flex-direction:column;gap:7px}
  .frame{border:1px solid var(--border);background:var(--bg);padding:6px 8px}
  .f-head{display:flex;justify-content:space-between;align-items:center;gap:6px;font-size:11px;flex-wrap:wrap}
  .f-head .code{font-weight:bold;color:#5be08a}
  .f-head .meta{color:var(--dim)}
  .f-head .acts2{display:flex;gap:4px;flex-shrink:0}
  .f-strip{margin-top:5px}
  .f-strip svg{display:block;width:100%;background:#000;border:1px solid var(--border)}
  .touchbar{position:fixed;bottom:0;left:0;width:100%;background:var(--hi);color:var(--hitext);padding:8px;border-top:1px solid var(--border);z-index:50;display:flex;justify-content:space-around;align-items:center;font-weight:bold;font-size:11px}
  .touchbar button{background:transparent;border:none;color:inherit;font-family:inherit;font-weight:bold;font-size:11px}
  #toast{position:fixed;bottom:60px;left:50%;transform:translateX(-50%);background:var(--panel);border:1px solid var(--border);color:var(--text);padding:8px 16px;font-size:12px;opacity:0;transition:opacity .2s;pointer-events:none;z-index:60;max-width:92%;text-align:center}
  #toast.show{opacity:1}
  @media(min-width:768px){
    .tabs{display:none}
    .grid{flex-direction:row;flex-wrap:wrap;align-items:flex-start}
    #sec-scope{width:62%;display:block !important}
    #sec-presets{width:34%;display:block !important}
    #sec-trames{width:100%;display:block !important}
  }
</style></head>
<body>
<div class="crt"></div>
<div class="app">

  <header class="dos-box">
    <span class="dos-title">┌─ SNIFFER RAUS RF MOBILE v2.9 ─┐</span>
    <div class="hd">
      <div>
        <div class="logos"><img class="r" src="/logo" alt="SNIFFER RAUS"><img class="s" src="/slca" alt="SLCA"></div>
        <div class="sub">CC1101 SPI · IP 192.168.4.1 · <span id="statut">connexion…</span></div>
      </div>
      <div class="right">
        <div class="st-cap" id="status-cap">○ HALT / PAUSE</div>
        <div class="st-rssi" id="rssi-val">-- dBm</div>
      </div>
    </div>
    <div class="themes">
      <button class="dos-btn sm" onclick="setTheme('dos')">DOS</button>
      <button class="dos-btn sm" onclick="setTheme('amiga')">AMIGA</button>
    </div>
  </header>

  <div class="tabs">
    <button id="tab-scope" class="tab-btn active" onclick="switchTab('scope')">[1] SCOPE</button>
    <button id="tab-presets" class="tab-btn" onclick="switchTab('presets')">[2] PRESETS</button>
    <button id="tab-trames" class="tab-btn" onclick="switchTab('trames')">[3] TRAMES</button>
  </div>

  <div class="grid">
    <section id="sec-scope" class="dos-box">
      <span class="dos-title">┌─ SCR 1 : SIGNAL 433.92 MHz ─┐</span>
      <div class="vu"><div class="fill" id="vufill"></div><span class="lbl" id="vulabel">--</span></div>
      <div class="scope-wrap">
        <canvas id="scope"></canvas>
        <div class="sc-label">-20 dBm</div>
        <div class="sc-cut" id="cutoff-label">SEUIL : -95 dBm</div>
      </div>
      <div class="ctrls">
        <div class="ctl">
          <label><span>SEUIL DE BRUIT (CUTOFF)</span><span class="v" id="cutoff-val">-95 dBm</span></label>
          <input type="range" id="cutoff-slider" min="-120" max="-20" value="-95" oninput="updateDSP()">
        </div>
        <div class="ctl">
          <label><span>GAIN SPECTRE (ÉCHELLE)</span><span class="v" id="scale-val">80 dB</span></label>
          <input type="range" id="scale-slider" min="20" max="120" value="80" oninput="updateDSP()">
        </div>
        <div class="ctl">
          <label><span>TEMPS (FENÊTRE)</span><span class="v" id="temps-val">8 s</span></label>
          <input type="range" id="temps-slider" min="2" max="12" value="8" oninput="updateDSP()">
        </div>
        <div class="ctl">
          <label><span>VOLUME (SON SIGNAL)</span><span class="v" id="vol-val">0</span></label>
          <input type="range" id="vol-slider" min="0" max="10" value="0" oninput="updateDSP()">
        </div>
      </div>
      <div class="acts">
        <button id="btn-toggle" class="dos-btn" onclick="toggleCapture()">&#9654; START</button>
        <button class="dos-btn" onclick="viderTrames()">&#10007; CLEAR</button>
      </div>
    </section>

    <section id="sec-presets" class="dos-box">
      <span class="dos-title">┌─ SCR 2 : CATALOGUE PRESETS ─┐</span>
      <div class="hint">[F5] émet la trame enregistrée · &#10007; l'efface.</div>
      <div class="cat-list" id="cat-list">chargement…</div>
      <div class="inject">
        <span class="lbl">INJECTEUR MANUEL HEX :</span>
        <div class="row">
          <input type="text" id="hex" placeholder="0x123ABC">
          <button class="dos-btn sm" onclick="injectHex()">ENVOYER</button>
        </div>
      </div>
    </section>

    <section id="sec-trames" class="dos-box">
      <span class="dos-title">┌─ SCR 3 : TRAMES CAPTURÉES ─┐</span>
      <div class="hint">Sauvegarder ou effacer une trame : [F9] SAVE · &#10007; retire.</div>
      <div class="frames" id="frames"><div class="hint">En écoute — activez CAPTURE…</div></div>
    </section>
  </div>

</div>

<footer class="touchbar">
  <button onclick="toggleCapture()">[F2] CAPTURE</button>
  <button onclick="emitLast()">[F5] EMETTRE</button>
  <button onclick="viderTrames()">[F8] EFFACER</button>
  <span>SNIFFER-RAUS</span>
</footer>
<div id="toast"></div>

<script>
let captureOn=false, dernier=0, lastCount=-1, echelle=80, cutoff=-95, yMax=-20, specBuf=[];
let lastFrame=null, dismissed={};
let volume=0, temps=8, rssiCur=null, audioCtx=null, audioGain=null;
let newCount=0, newTime=0;
const scope=document.getElementById("scope"), c2=scope.getContext("2d");
let W=0,H=0;
function fit(){W=scope.parentElement.clientWidth;H=scope.parentElement.clientHeight;scope.width=W;scope.height=H;}
window.addEventListener("resize",fit);fit();

const rH=20;
function mapY(db){const yMin=yMax-echelle;return (db-yMax)/(yMin-yMax)*(H-rH);}
function sc(){return getComputedStyle(document.body);}
function toast(m){const t=document.getElementById("toast");t.textContent=m;t.className="show";clearTimeout(t._h);t._h=setTimeout(function(){t.className="";},1800);}
function ensureAudio(){
  if(audioCtx)return;
  try{
    audioCtx=new (window.AudioContext||window.webkitAudioContext)();
    const len=Math.floor(audioCtx.sampleRate*0.5);
    const buf=audioCtx.createBuffer(1,len,audioCtx.sampleRate);
    const d=buf.getChannelData(0);
    for(let i=0;i<len;i++)d[i]=Math.random()*2-1;
    const src=audioCtx.createBufferSource();src.buffer=buf;src.loop=true;
    const filt=audioCtx.createBiquadFilter();filt.type="lowpass";filt.frequency.value=2500;
    audioGain=audioCtx.createGain();audioGain.gain.value=0;
    src.connect(filt);filt.connect(audioGain);audioGain.connect(audioCtx.destination);
    src.start();
    if(audioCtx.state==="suspended")audioCtx.resume();
  }catch(e){audioCtx=null;audioGain=null;}
}
function setAudioLevel(){
  if(!audioGain)return;
  const r=(rssiCur==null)?-100:rssiCur;
  const env=Math.max(0,Math.min(1,(r+110)/90));
  const g=env*env*(volume/10)*1.2;
  audioGain.gain.setTargetAtTime(g,audioCtx.currentTime,0.06);
}

function drawScope(){
  if(!W||!H)return;
  c2.fillStyle="#000";c2.fillRect(0,0,W,H);
  c2.strokeStyle="rgba(255,255,255,0.08)";c2.lineWidth=1;
  for(let x=0;x<W;x+=25){c2.beginPath();c2.moveTo(x,0);c2.lineTo(x,H);c2.stroke();}
  for(let y=0;y<H-rH;y+=20){c2.beginPath();c2.moveTo(0,y);c2.lineTo(W,y);c2.stroke();}
  const cutY=Math.min(H-1,Math.max(0,mapY(cutoff)));
  c2.strokeStyle=sc().getPropertyValue("--danger").trim()||"#ff5555";c2.setLineDash([3,3]);
  c2.beginPath();c2.moveTo(0,cutY);c2.lineTo(W,cutY);c2.stroke();c2.setLineDash([]);
  if(specBuf.length>1){
    c2.strokeStyle=sc().getPropertyValue("--border").trim()||"#ffffff";c2.lineWidth=2;c2.beginPath();
    const virtNewest=newCount+(performance.now()-newTime)/25;
    const winLen=temps/0.025;
    const base=(specBuf.length-1)-newCount;
    for(let x=0;x<W;x++){
      const g=(virtNewest-winLen)+(x/W)*winLen;
      const i=base+g;const i0=Math.max(0,Math.floor(i));const i1=Math.min(specBuf.length-1,i0+1);const fr=i-i0;
      const db=specBuf[i0]+(specBuf[i1]-specBuf[i0])*fr;
      const y=Math.min(H-rH-1,Math.max(0,mapY(db)));
      if(x===0)c2.moveTo(x,y);else c2.lineTo(x,y);
    }
    c2.stroke();
  }
  // axe horizontal : TEMPS (droite = maintenant, gauche = passé)
  const spanSec=temps;
  const accent=sc().getPropertyValue("--accent").trim()||"#ffff55";
  const dim=sc().getPropertyValue("--dim").trim()||"#aaaaaa";
  c2.fillStyle="rgba(0,0,0,0.45)";c2.fillRect(0,H-rH,W,rH);
  c2.strokeStyle=accent;c2.lineWidth=1;
  c2.beginPath();c2.moveTo(0,H-rH+0.5);c2.lineTo(W,H-rH+0.5);c2.stroke();
  const step=(spanSec>=6)?2:1;
  c2.font="bold 10px monospace";c2.fillStyle=accent;
  for(let t=0;t<=spanSec-0.001;t+=step){
    const x=Math.round(W-(t/spanSec)*W);
    c2.beginPath();c2.moveTo(x,H-rH);c2.lineTo(x,H-rH+5);c2.stroke();
    c2.fillText("-"+t+"s", x+2, H-rH+15);
  }
  c2.fillText("0s", W-16, H-rH+15);
  c2.fillStyle=dim;c2.font="9px monospace";
  c2.fillText("TEMPS", 4, H-rH+6);
  requestAnimationFrame(drawScope);
}
drawScope();

function append(j){
  if(!j.histo)return;
  if(lastCount<0)lastCount=j.hcount-j.histo.length;
  let n=j.hcount-lastCount;if(n<=0)return;if(n>j.histo.length)n=j.histo.length;
  const tail=j.histo.slice(j.histo.length-n);
  for(const v of tail)specBuf.push(v);
  if(specBuf.length>512)specBuf=specBuf.slice(specBuf.length-512);
  lastCount=j.hcount;
  newCount=j.hcount-1; newTime=performance.now();
}

function majCap(){
  const cap=document.getElementById("status-cap");
  const b=document.getElementById("btn-toggle");
  if(captureOn){cap.textContent="● CAPTURE ACTIVE";cap.className="st-cap";b.textContent="❚❚ PAUSE";}
  else{cap.textContent="○ HALT / PAUSE";cap.className="st-cap off";b.textContent="▶ START";}
}
function toggleCapture(){
  captureOn=!captureOn;majCap();
  if(captureOn){document.getElementById("frames").innerHTML="";dernier=0;lastFrame=null;}
  fetch("/capture?on="+(captureOn?1:0)).catch(function(){});
}

function dessiner(d){
  const t=d.reduce(function(a,b){return a+b;},0)||1;const ech=1000/t;
  let s="",x=0;
  d.forEach(function(v,i){const w=v*ech;const c=(i%2===0)?"#87CEEB":"#1b232e";
    s+='<rect x="'+x.toFixed(1)+'" y="2" width="'+w.toFixed(1)+'" height="26" fill="'+c+'"><title>'+v+' µs</title></rect>';x+=w;});
  return '<svg viewBox="0 0 1000 30"><rect width="1000" height="30" fill="#000"/>'+s+'</svg>';
}

function ligne(f){
  if(dismissed[f.id])return;
  const el=document.createElement("div");el.className="frame";
  el.innerHTML='<div class="f-head"><span><span class="code">0x'+f.valeur+'</span> <span class="meta">'+f.nb+' imp · ~'+f.bp+' µs · '+f.classe+'</span></span>'
    +'<span class="acts2"><button class="dos-btn sm" data-save="'+f.id+'">[F9] SAVE</button><button class="dos-btn sm rec" data-del="'+f.id+'">&#10007;</button></span></div>'
    +'<div class="f-strip">'+dessiner(f.d)+'</div>';
  el.querySelector("[data-save]").onclick=function(){
    const n=prompt("Nommer cette capture :","0x"+f.valeur+" #"+f.id);
    if(n===null)return;const nom=n.trim()||("capture "+f.id);
    fetch("/enregistrer?id="+f.id+"&nom="+encodeURIComponent(nom)).then(function(){toast("Enregistré : "+nom);charger();});
  };
  el.querySelector("[data-del]").onclick=function(){dismissed[f.id]=true;el.remove();};
  document.getElementById("frames").prepend(el);
  while(document.getElementById("frames").children.length>30)document.getElementById("frames").lastChild.remove();
  lastFrame=f;
}

function viderTrames(){document.getElementById("frames").innerHTML="";}

async function tick(){
  try{
    const r=await fetch("/data");const j=await r.json();
    document.getElementById("statut").textContent=j.statut;
    rssiCur=(j.rssi!=null)?j.rssi:null;
    document.getElementById("rssi-val").textContent=(j.rssi!=null?Math.round(j.rssi)+" dBm":"--");
    setAudioLevel();
    const v=Math.max(0,Math.min(100,j.niveau||0));
    document.getElementById("vufill").style.width=v+"%";
    document.getElementById("vulabel").textContent=(j.rssi!=null?Math.round(j.rssi)+" dBm":"--");
    if(j.capture!==undefined&&j.capture!==captureOn){captureOn=j.capture;majCap();}
    append(j);
    if(j.frames&&j.frames.length&&captureOn){
      const recus=j.frames.filter(function(f){return f.id>dernier;}).sort(function(a,b){return a.id-b.id;});
      for(const f of recus)ligne(f);
      if(recus.length)dernier=Math.max(dernier,j.frames.map(function(f){return f.id;}).reduce(function(a,b){return Math.max(a,b);},dernier));
    }
  }catch(e){}
}

function setTheme(name){
  document.body.className="";if(name==="amiga")document.body.classList.add("theme-amiga");
}
function switchTab(tab){
  const secs={scope:document.getElementById("sec-scope"),presets:document.getElementById("sec-presets"),trames:document.getElementById("sec-trames")};
  for(const k in secs)secs[k].style.display="none";
  secs[tab].style.display="block";
  const btns={scope:"tab-scope",presets:"tab-presets",trames:"tab-trames"};
  for(const k in btns)document.getElementById(btns[k]).classList.remove("active");
  document.getElementById(btns[tab]).classList.add("active");
}

function updateDSP(){
  cutoff=parseInt(document.getElementById("cutoff-slider").value);
  echelle=parseInt(document.getElementById("scale-slider").value);
  document.getElementById("cutoff-val").textContent=cutoff+" dBm";
  document.getElementById("scale-val").textContent=echelle+" dB";
  document.getElementById("cutoff-label").textContent="SEUIL : "+cutoff+" dBm";
  temps=parseInt(document.getElementById("temps-slider").value);
  document.getElementById("temps-val").textContent=temps+" s";
  volume=parseInt(document.getElementById("vol-slider").value);
  document.getElementById("vol-val").textContent=volume;
  if(volume>0){ensureAudio();setAudioLevel();}else if(audioGain){audioGain.gain.setTargetAtTime(0,audioCtx.currentTime,0.05);}
}

function injectHex(){
  const i=document.getElementById("hex");const val=i.value.trim();
  if(!val){toast("Veuillez saisir un code hex.");return;}
  document.getElementById("frames").insertAdjacentHTML("afterbegin",'<div class="frame"><div class="f-head"><span class="code">'+val+'</span> <span class="meta">injection manuelle</span></div></div>');
  i.value="";toast("Hex noté : "+val);
}

function emitLast(){
  if(!lastFrame){toast("Aucune capture en mémoire. Activez CAPTURE.");return;}
  toast("Pour émettre : [F9] SAVE sur une trame puis [F5] dans le catalogue.");
}

async function charger(){
  try{
    const r=await fetch("/catalogue");const j=await r.json();
    const c=document.getElementById("cat-list");
    if(!j.catalogue.length){c.innerHTML='<div class="hint">catalogue vide</div>';return;}
    c.innerHTML=j.catalogue.map(function(e){return '<div class="cat-item"><span class="nm">'+e.nom+'</span><span class="btns">'
      +(e.del?'<button class="dos-btn sm rec" data-del="'+e.id+'">&#10007;</button>':'')
      +'<button class="dos-btn sm" data-id="'+e.id+'">[F5]</button></span></div>';}).join("");
    c.querySelectorAll("[data-id]").forEach(function(b){b.onclick=async function(){b.textContent="…";await fetch("/envoyer?id="+b.dataset.id);toast("Émission envoyée");setTimeout(charger,600);};});
    c.querySelectorAll("[data-del]").forEach(function(b){b.onclick=async function(){await fetch("/supprimer?id="+b.dataset.del);toast("Effacé");charger();};});
  }catch(e){}
}

switchTab("scope");setInterval(tick,150);tick();charger();majCap();
</script>
</body></html>
)rawliteral";// ---- Routes ----------------------------------------------------------------
void servir_logo_raus() {
  serveur.send_P(200, "image/jpeg", (const char*)LOGO_RAUS, LOGO_RAUS_LEN);
}
void servir_logo_slca() {
  serveur.send_P(200, "image/png", (const char*)LOGO_SLCA, LOGO_SLCA_LEN);
}

void gererData() {
  serveur.sendHeader("Cache-Control", "no-store");
  String json = "{\"niveau\":" + String(niveau) +
                ",\"rssi\":" + String(rssiActuel, 1) +
                ",\"radio\":" + String(radioOk ? "true" : "false") +
                ",\"trames\":" + String(nbTrames) +
                ",\"brutes\":" + String(totalBrut) +
                ",\"actif\":" + String(micros() - tPrec < ECART_TRAME ? "true" : "false") +
                ",\"statut\":\"" + String(radioOk ? "CC1101 prêt · 433.92 MHz" : "CC1101 absent !") + "\""
                ",\"capture\":" + String(captureActive ? "true" : "false") +
                ",\"frames\":[";
  for (int i = 0; i < nbTrames; i++) {
    Trame &t = trames[i];
    json += "{\"id\":" + String(t.id) +
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
  json += "],\"histo\":[";
  for (int i = 0; i < HISTO_LEN; i++) { if (i) json += ","; json += String(histoDb[(histoIdx + i) % HISTO_LEN]); }
  json += "],\"hcount\":" + String(hcount);
  json += "}";
  serveur.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(SSID, MOT_DE_PASSE);

  // --- CC1101 : SPI + OOK asynchrone ---
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  SPI.end();                                   // reset d'un etat antérieur
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);  // broches explicites ESP32
  delay(5);
  int etat = radio.begin(433.92);
  if (etat == RADIOLIB_ERR_NONE) {
    radioOk = true;
    radio.setOOK(true);
    configurerRXAsync();
    Serial.println("CC1101 OK - OOK asynchrone 433.92 MHz");
    // Vérification du MISO (lectures) : maintenant ça doit répondre
    Serial.printf("[CHIP] PARTNUM=0x%02X VERSION=0x%02X (attendu 0x00/0x14)\n",
                  ccLit(0x30), ccLit(0x31));
    float r0 = radio.getRSSI(), r1 = radio.getRSSI(), r2 = radio.getRSSI();
    Serial.printf("[RSSI] 3 échantillons : %.0f / %.0f / %.0f dBm\n", r0, r1, r2);
  } else {
    // Le MISO est HS : begin() echoue dans sa verification, mais la puce
    // repond aux ECRITURES. On configure donc par ecritures seules.
    radio.setFrequency(433.92);
    radio.setOOK(true);
    configurerRXAsync();
    radioOk = true;
    Serial.printf("CC1101 init forcee (code %d) : ecritures seules, reception OOK active\n", etat);
  }

  pinMode(PIN_RX, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_RX), surChangement, CHANGE);

  prefs.begin("radar433", false);
  chargerCaptures();

  serveur.on("/", []() {
    serveur.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    serveur.send(200, "text/html", PAGE);
  });
  serveur.on("/data", gererData);
  serveur.on("/catalogue", gererCatalogue);
  serveur.on("/envoyer", gererEnvoyer);
  serveur.on("/enregistrer", gererEnregistrer);
  serveur.on("/supprimer", gererSupprimer);
  serveur.on("/capture", gererCapture);
  serveur.on("/logo", servir_logo_raus);
  serveur.on("/slca", servir_logo_slca);
  serveur.begin();

  Serial.println();
  Serial.print("Réseau WiFi : ");
  Serial.println(SSID);
  Serial.println("Adresse : http://192.168.4.1");
}

void loop() {
  // vumètre : niveau RÉEL depuis le registre RSSI (enfin lisible)
  static unsigned long tMetre = 0;
  if (millis() - tMetre >= 25) {
    tMetre = millis();
    if (radioOk) {
      int8_t raw = (int8_t)ccLit(0x34);            // registre RSSI, signé
      rssiActuel = raw / 2.0f - 74.0f;             // dBm approximatif
      float inst = (raw + 50.0f) * 100.0f / 150.0f; // -50 -> 0 %, +100 -> 100 %
      if (inst > picNiv) picNiv = inst;            // attaque rapide
      else { picNiv -= 2.0f; if (picNiv < 0) picNiv = 0; }
      niveau = (int)picNiv;
      histoDb[histoIdx] = (int)rssiActuel;
      histoIdx = (histoIdx + 1) % HISTO_LEN;
      hcount++;
    }
  }

  // diagnostic brut : le front bouge-t-il ?
  static unsigned long tBeat = 0;
  if (millis() - tBeat >= 2000) {
    tBeat = millis();
    Serial.printf("[RAW] bursts=%lu | GDO0=%d | p=%d | RSSI=%.0f dBm | regRSSI=0x%02X\n",
                  totalBrut, digitalRead(PIN_RX), nbPulses, rssiActuel, ccLit(0x34));
  }

  // finaliser la trame en cours si le signal s'est arrêté
  if (nbPulses > 0 && tPrec != 0 && (micros() - tPrec) > ECART_TRAME) {
    finaliserTrame();
  }
  serveur.handleClient();
}