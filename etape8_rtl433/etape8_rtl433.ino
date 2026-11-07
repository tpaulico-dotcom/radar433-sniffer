/* RADAR 433 — étape 8 : rtl_433_ESP (décodeurs pro) + serveur WiFi + UI DOS.
   rtl_433_ESP fait la réception + le décodage (~328 protocoles).
   On garde : WiFi AP, UI DOS/AMIGA, spectro/vumètre/son, catalogue, rejeu.
   Rejeu TX = bascule RX->TX du CC1101 (spiTake/spiGive) + bit-bang GDO0. */

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SPI.h>
#include <rtl_433_ESP.h>
#include "catalogue.h"
#include "logos.h"
#include "logo_sniffer.h"
#include "logo_atari.h"
#include "page8.h"

#define PIN_DATA 4          // GDO0 (GPIO4) — data + rejeu
#define PIN_CS   5

const char* SSID         = "iPhone de Joe";
const char* MOT_DE_PASSE = "";

// ---- Événements décodés ----
#define MAX_EVENTS 25
struct Evenement {
  unsigned long no;
  char modele[32];
  unsigned long devid;
  int cmd;
  int rssi;
  float temp;
  int hum;
  char heure[9];
  uint16_t d[200];
  int dn;
};
Evenement evt[MAX_EVENTS];
int nbEvt = 0;
unsigned long numeroEvt = 0;
unsigned long resetPending = 0;

char dernierJson[512];
unsigned long dernierT = 0;

// ---- Dernière séquence brute captée (pour enregistrer + rejouer) ----
#define MAX_RAW 400
uint16_t derniereSeq[MAX_RAW];
volatile int derniereNb = 0;
volatile bool brutPret = false;
volatile unsigned long brutD = 0;
volatile int brutR = -100;
volatile unsigned long brutT = 0;

// ---- RSSI / spectro / vumètre ----
#define HISTO_LEN 512
int histoDb[HISTO_LEN];
int histoIdx = 0;
unsigned long hcount = 0;
int rssiActuel = -100;
float picNiv = 0.0f;
int niveau = 0;
volatile bool captureActive = false;
int nbBrut = 0;
int nbDecode = 0;   // démarre en PAUSE

rtl_433_ESP rf;
char msgBuf[512];
WebServer serveur(80);
Preferences prefs;

// ---- Catalogue utilisateur (NVS) ----
#define MAX_CAPTURES_UTIL 16
struct CaptureUtilisateur { char nom[28]; uint16_t seq[200]; int nb; };
CaptureUtilisateur captU[MAX_CAPTURES_UTIL];
int nbCaptU = 0;

void chargerCaptures() {
  nbCaptU = prefs.getInt("nb", 0);
  if (nbCaptU > MAX_CAPTURES_UTIL) nbCaptU = MAX_CAPTURES_UTIL;
  for (int i = 0; i < nbCaptU; i++) {
    String n = prefs.getString(("n" + String(i)).c_str(), "");
    n.toCharArray(captU[i].nom, 28);
    String s = prefs.getString(("s" + String(i)).c_str(), "");
    int nb = 0, deb = 0;
    while (nb < 200) {
      int v = s.indexOf(',', deb);
      String val = (v < 0) ? s.substring(deb) : s.substring(deb, v);
      if (val.length() == 0) break;
      captU[i].seq[nb++] = val.toInt();
      if (v < 0) break;
      deb = v + 1;
    }
    captU[i].nb = nb;
  }
}

// ---- SPI brut (pour reconfigurer le CC1101 en TX) ----
SPISettings ccSpi(4000000, MSBFIRST, SPI_MODE0);
void ccEcrit(uint8_t reg, uint8_t val) {
  SPI.beginTransaction(ccSpi);
  digitalWrite(PIN_CS, LOW);
  delayMicroseconds(2);
  SPI.transfer(reg & 0x3F);
  SPI.transfer(val);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();
}
uint8_t ccLire(uint8_t addr) {
  SPI.beginTransaction(ccSpi);
  digitalWrite(PIN_CS, LOW);
  delayMicroseconds(2);
  SPI.transfer((addr >= 0x30) ? (addr | 0xC0) : (addr | 0x80));   // 0xC0=statut burst, 0x80=lecture
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

// ---- Callbacks rtl_433_ESP ----
void extraire(const char* j, char* modele, unsigned long* id, int* cmd, int* rssi, float* temp, int* hum) {
  const char* p = strstr(j, "\"model\":\"");
  if (p) { p += 9; int i = 0; while (*p && *p != '"' && i < 31) modele[i++] = *p++; modele[i] = 0; }
  else strcpy(modele, "?");
  p = strstr(j, "\"id\":");   *id   = p ? strtoul(p + 5, 0, 10) : 0;
  p = strstr(j, "\"cmd\":");  *cmd  = p ? atoi(p + 6) : 0;
  p = strstr(j, "\"rssi\":"); *rssi = p ? atoi(p + 7) : 0;
  p = strstr(j, "\"temperature_C\":"); *temp = p ? atof(p + 16) : -999;
  p = strstr(j, "\"humidity\":");      *hum  = p ? atoi(p + 11) : -1;
}

void onDecode(char* m) {
  char modele[32]; unsigned long devid; int cmd, rssi; float temp; int hum;
  extraire(m, modele, &devid, &cmd, &rssi, &temp, &hum);
  char cle[48];
  snprintf(cle, sizeof(cle), "%s:%lu:%d", modele, devid, cmd);
  if (strcmp(cle, dernierJson) == 0 && (millis() - dernierT) < 1500) return;
  strncpy(dernierJson, cle, sizeof(dernierJson) - 1); dernierJson[sizeof(dernierJson) - 1] = 0;
  dernierT = millis();
  Serial.print("DECODE: "); Serial.println(m);
  nbDecode++;
  if (!captureActive) return;

  Evenement e; e.no = ++numeroEvt;
  strcpy(e.modele, modele); e.devid = devid; e.cmd = cmd; e.rssi = rssi; e.temp = temp; e.hum = hum;
  unsigned long now = millis();
  snprintf(e.heure, 9, "%02lu:%02lu:%02lu", (now / 3600000UL) % 24, (now / 60000UL) % 60, (now / 1000UL) % 60);
  e.dn = derniereNb < 200 ? derniereNb : 200;
  for (int k = 0; k < e.dn; k++) e.d[k] = derniereSeq[k];

  if (nbEvt < MAX_EVENTS) nbEvt++;
  for (int i = nbEvt - 1; i > 0; i--) evt[i] = evt[i - 1];
  evt[0] = e;
}

// ---- Décodeur CAME 12 bits (validé sur les 4096 codes) ----
bool decodeCAME(const uint16_t* s, int n, unsigned int& code) {
  for (int si = 0; si < n - 26; si++) {
    if (s[si] < 8000 || s[si] > 16000) continue;      // sync ~11520
    for (int d = 1; d <= 2; d++) {
      int base = si + d;
      if (base + 24 >= n) continue;
      if (s[base] < 200 || s[base] > 480) continue;   // start ~320
      unsigned int c = 0; bool ok = true;
      for (int k = 1; k <= 12; k++) {
        uint16_t h = s[base + 2 * k];
        if (h >= 200 && h <= 480) c = (c << 1) | 1;   // 320 = 1
        else if (h >= 520 && h <= 820) c = (c << 1);  // 640 = 0
        else { ok = false; break; }
      }
      if (ok) { code = c; return true; }
    }
  }
  return false;
}

void onRaw(const int* p, const int* g, unsigned int n, unsigned long d, int r) {
  if (n == 0) return;
  int m = 0;
  for (unsigned int i = 0; i < n && m < MAX_RAW - 2; i++) {
    derniereSeq[m++] = (uint16_t)(p[i] > 0 ? p[i] : 0);
    derniereSeq[m++] = (uint16_t)(g[i] > 0 ? g[i] : 0);
  }
  derniereNb = m;
  brutD = d; brutR = r;
  brutT = millis();
  brutPret = true;   // signale au loop (cœur 0) qu'un brut est prêt
  // ---- décodage CAME custom (en plus du brut) ----
  {
    unsigned int camCode;
    if (decodeCAME(derniereSeq, derniereNb, camCode)) {
      char cle[32]; snprintf(cle, sizeof(cle), "CAME:%u:0", camCode);
      if (!(strcmp(cle, dernierJson) == 0 && (millis() - dernierT) < 1500)) {
        strncpy(dernierJson, cle, sizeof(dernierJson) - 1); dernierJson[sizeof(dernierJson) - 1] = 0;
        dernierT = millis();
        nbDecode++;
        if (captureActive) {
          Evenement e; e.no = ++numeroEvt;
          strcpy(e.modele, "CAME"); e.devid = camCode; e.cmd = 0; e.rssi = r; e.temp = -999; e.hum = -1;
          unsigned long now = millis();
          snprintf(e.heure, 9, "%02lu:%02lu:%02lu", (now / 3600000UL) % 24, (now / 60000UL) % 60, (now / 1000UL) % 60);
          e.dn = derniereNb < 200 ? derniereNb : 200;
          for (int k = 0; k < e.dn; k++) e.d[k] = derniereSeq[k];
          if (nbEvt < MAX_EVENTS) nbEvt++;
          for (int z = nbEvt - 1; z > 0; z--) evt[z] = evt[z - 1];
          evt[0] = e;
        }
      }
    }
  }

}
// ---- Rejeu TX ----
void envoyerSequence(const uint16_t* seq, int n) {
  if (n <= 0) return;
  Serial.printf("[TX] start n=%d\n", n);
  rf.disableReceiver();
  if (!rtl_433_ESP::spiTake(300)) { Serial.println("[TX] spiTake FAIL"); rf.enableReceiver(); return; }
  rf.setCC1101Register(0x08, 0x32);   // PKTCTRL0 = asynchrone
  rf.setCC1101Register(0x02, 0x2D);   // IOCFG0 = GDO0 entree data TX
  ccCommande(0x35);                   // STX
  pinMode(PIN_DATA, OUTPUT);
  for (int rep = 0; rep < 3; rep++) {
    for (int i = 0; i < n; i++) {
      digitalWrite(PIN_DATA, (i % 2 == 0) ? HIGH : LOW);
      delayMicroseconds(seq[i]);
    }
    digitalWrite(PIN_DATA, LOW);
    delay(20);
    yield();
  }
  pinMode(PIN_DATA, INPUT);
  ccCommande(0x36);                   // SIDLE
  rtl_433_ESP::spiGive();
  // === RE-INIT COMPLETE du CC1101 (restaure le RX proprement) ===
  rf.end();
  rf.initReceiver(PIN_DATA, 433.92);
  rf.enableReceiver();
  Serial.println("[TX] end (reinit)");
}

// ---- Routes ----
void gererData() {
  serveur.sendHeader("Cache-Control", "no-store");
  String j = "{\"niveau\":" + String(niveau) +
             ",\"rssi\":" + String(rssiActuel) +
             ",\"radio\":true" +
             ",\"capture\":" + String(captureActive ? "true" : "false") +
             ",\"statut\":\"CC1101 · rtl_433 · 433.92 MHz\"" +
             ",\"devents\":" + String(nbEvt) +
             ",\"raw\":" + String(derniereNb) +
             ",\"events\":[";
  for (int i = 0; i < nbEvt; i++) {
    if (i) j += ",";
    j += "{\"no\":" + String(evt[i].no) +
         ",\"model\":\"" + String(evt[i].modele) + "\"" +
         ",\"id\":" + String(evt[i].devid) +
         ",\"cmd\":" + String(evt[i].cmd) +
         ",\"rssi\":" + String(evt[i].rssi) + ",\"temp\":" + String(evt[i].temp, 1) + ",\"hum\":" + String(evt[i].hum) +
         ",\"heure\":\"" + String(evt[i].heure) + "\"" +
         ",\"d\":[";
    int dn2 = evt[i].dn < 50 ? evt[i].dn : 50;
    for (int k = 0; k < dn2; k++) { if (k) j += ","; j += String(evt[i].d[k]); }
    j += "]}";
  }
  j += "],\"histo\":[";
  for (int i = 0; i < HISTO_LEN; i++) { if (i) j += ","; j += String(histoDb[(histoIdx + i) % HISTO_LEN]); }
  j += "],\"hcount\":" + String(hcount) + "}";
  serveur.send(200, "application/json", j);
}

void gererCatalogue() {
  String j = "{\"catalogue\":[";
  bool premier = true;
  for (int i = 0; i < NB_CATALOGUE; i++) {
    if (!premier) j += ","; premier = false;
    j += "{\"id\":" + String(i)
       + ",\"nom\":\"" + String(CATALOGUE[i].nom) + "\""
       + ",\"marque\":\"" + String(CATALOGUE[i].marque) + "\""
       + ",\"cat\":" + String(CATALOGUE[i].cat) + "}";
  }
  for (int i = nbCaptU - 1; i >= 0; i--) {
    if (!premier) j += ","; premier = false;
    j += "{\"id\":" + String(NB_CATALOGUE + i)
       + ",\"nom\":\"" + String(captU[i].nom) + "\""
       + ",\"marque\":\"\",\"cat\":-1,\"del\":true}";
  }
  j += "]}";
  serveur.send(200, "application/json", j);
}

void gererEnvoyer() {
  int id = serveur.arg("id").toInt();
  if (id >= 0 && id < NB_CATALOGUE) {
    envoyerSequence(CATALOGUE[id].seq, CATALOGUE[id].nb);
  } else if (id >= NB_CATALOGUE && id < NB_CATALOGUE + nbCaptU) {
    envoyerSequence(captU[id - NB_CATALOGUE].seq, captU[id - NB_CATALOGUE].nb);
  }
  serveur.send(200, "application/json", "{\"ok\":true}");
}

void gererRejouer() {
  unsigned long no = serveur.arg("no").toInt();
  for (int i = 0; i < nbEvt; i++) {
    if (evt[i].no == no && evt[i].dn >= 4) {
      envoyerSequence(evt[i].d, evt[i].dn);
      serveur.send(200, "application/json", "{\"ok\":true}");
      return;
    }
  }
  serveur.send(200, "application/json", "{\"ok\":false}");
}

void gererEnregistrer() {
  String nomStr = serveur.arg("nom");
  const char* nom = nomStr.c_str();
  unsigned long no = serveur.arg("no").toInt();
  int idx = -1;
  for (int i = 0; i < nbEvt; i++) { if (evt[i].no == no) { idx = i; break; } }
  Serial.printf("[SAVE] no=%lu idx=%d dn=%d nb=%d nom='%s'\n", no, idx, (idx >= 0 ? evt[idx].dn : -1), nbCaptU, (nom ? nom : "NULL"));
  if (nomStr.length() == 0 || idx < 0 || evt[idx].dn < 4 || nbCaptU >= MAX_CAPTURES_UTIL) {
    Serial.println("[SAVE] -> ECHEC");
    serveur.send(200, "application/json", "{\"ok\":false}");
    return;
  }
  int i = nbCaptU;
  strncpy(captU[i].nom, nom, 27); captU[i].nom[27] = 0;
  int n = evt[idx].dn < 200 ? evt[idx].dn : 200;
  captU[i].nb = n;
  for (int k = 0; k < n; k++) captU[i].seq[k] = evt[idx].d[k];
  nbCaptU++;
  prefs.putInt("nb", nbCaptU);
  prefs.putString(("n" + String(i)).c_str(), String(captU[i].nom));
  String s = "";
  for (int k = 0; k < n; k++) { if (k) s += ","; s += String(captU[i].seq[k]); }
  prefs.putString(("s" + String(i)).c_str(), s);
  Serial.println("[SAVE] -> OK");
  serveur.send(200, "application/json", "{\"ok\":true}");
}

void gererManuel() {
  String fmt = serveur.arg("fmt");
  String data = serveur.arg("plain");
  if (data.length() == 0) data = serveur.arg("data");
  uint16_t seq[400];
  int n = 0;
  if (fmt == "bin") {
    int bit = serveur.arg("bit").toInt(); if (bit < 20 || bit > 5000) bit = 300;
    int run = 0; char prev = 0;
    for (unsigned int i = 0; i < data.length() && n < 398; i++) {
      char c = data.charAt(i);
      if (c == '0' || c == '1') {
        if (c == prev) run++;
        else { if (run > 0) seq[n++] = (uint16_t)(run * bit); run = 1; prev = c; }
      }
    }
    if (run > 0) seq[n++] = (uint16_t)(run * bit);
  } else {
    unsigned int pos = 0;
    while (pos < data.length() && n < 398) {
      while (pos < data.length() && !isxdigit(data.charAt(pos))) pos++;
      if (pos >= data.length()) break;
      String h = "";
      while (pos < data.length() && isxdigit(data.charAt(pos))) h += data.charAt(pos++);
      unsigned long v = strtoul(h.c_str(), 0, 16);
      if (v > 0 && v < 65536) seq[n++] = (uint16_t)v;
    }
  }
  Serial.printf("[MAN] fmt=%s n=%d\n", fmt.c_str(), n);
  if (n < 4) { serveur.send(200, "application/json", "{\"ok\":false}"); return; }
  envoyerSequence(seq, n);
  serveur.send(200, "application/json", "{\"ok\":true}");
}

void gererSupprimer() {
  int id = serveur.arg("id").toInt();
  int i = id - NB_CATALOGUE;
  if (i < 0 || i >= nbCaptU) { serveur.send(400, "application/json", "{\"ok\":false}"); return; }
  for (int k = i; k < nbCaptU - 1; k++) captU[k] = captU[k + 1];
  nbCaptU--;
  prefs.putInt("nb", nbCaptU);
  for (int k = 0; k < nbCaptU; k++) {
    prefs.putString(("n" + String(k)).c_str(), String(captU[k].nom));
    String ss = "";
    for (int q = 0; q < captU[k].nb; q++) { if (q) ss += ","; ss += String(captU[k].seq[q]); }
    prefs.putString(("s" + String(k)).c_str(), ss);
  }
  serveur.send(200, "application/json", "{\"ok\":true}");
}

void gererCapture() {
  captureActive = (serveur.arg("on").toInt() != 0);
  Serial.printf("[CAPTURE] %s\n", captureActive ? "ON" : "OFF");
  serveur.send(200, "application/json", captureActive ? "{\"capture\":true}" : "{\"capture\":false}");
}

void servir_logo_raus() { serveur.send_P(200, "image/png", (const char*)LOGO_SNIFFER, LOGO_SNIFFER_LEN); }
void servir_logo_slca() { serveur.send_P(200, "image/png", (const char*)LOGO_SLCA, LOGO_SLCA_LEN); }
void servir_logo_atari() { serveur.send_P(200, "image/png", (const char*)LOGO_ATARI, LOGO_ATARI_LEN); }


// ---- Fiche HTML autonome d'une capture du catalogue (sauvegardable sur téléphone) ----
void gererFiche() {
  int id = serveur.arg("id").toInt();
  int i = id - NB_CATALOGUE;
  if (i < 0 || i >= nbCaptU) { serveur.send(404, "text/plain", "Fiche introuvable"); return; }
  int n = captU[i].nb;
  unsigned long total = 0;
  for (int k = 0; k < n; k++) total += captU[i].seq[k];
  int moy = n ? (int)(total / n) : 0;
  String csv = "";
  for (int k = 0; k < n; k++) { if (k) csv += ","; csv += String(captU[i].seq[k]); }
  String hex = "";
  char buf[8];
  for (int k = 0; k < n; k++) { snprintf(buf, sizeof(buf), "%04X", captU[i].seq[k]); hex += buf; hex += ((k & 7) == 7) ? "\n" : " "; }
  String nm = String(captU[i].nom);
  nm.replace("&", "&amp;"); nm.replace("<", "&lt;"); nm.replace(">", "&gt;");

  String h = "<!DOCTYPE html><html lang=\"fr\"><head><meta charset=\"utf-8\">";
  h += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
  h += "<title>FICHE - " + nm + "</title><style>";
  h += "body{font-family:monospace;background:#f0f0f0;color:#000;margin:0;padding:16px;max-width:900px;margin:auto}";
  h += "h1{font-size:15px;letter-spacing:3px;margin:0 0 4px;border-bottom:2px solid #000;padding-bottom:6px}";
  h += "h2{font-size:20px;margin:10px 0}";
  h += "table{border-collapse:collapse;margin:10px 0;font-size:13px}";
  h += "td{border:1px solid #000;padding:4px 10px}.k{font-weight:bold;background:#e0e0e0}";
  h += "pre{background:#fff;border:1px solid #000;padding:8px;font-size:11px;overflow:auto;white-space:pre-wrap;word-break:break-all}";
  h += "button{background:#008000;color:#fff;border:0;padding:8px 14px;font-family:monospace;font-size:13px;margin:6px 0;cursor:pointer}";
  h += ".note{font-size:11px;color:#444;margin-top:14px;border-top:1px dashed #888;padding-top:8px}";
  h += "</style></head><body>";
  h += "<h1>RAUS-SNIFFER // FICHE SIGNAL</h1>";
  h += "<h2>" + nm + "</h2><table>";
  h += "<tr><td class=\"k\">Impulsions</td><td>" + String(n) + "</td></tr>";
  h += "<tr><td class=\"k\">Durée totale</td><td>" + String(total) + " µs</td></tr>";
  h += "<tr><td class=\"k\">Pulse moyen</td><td>" + String(moy) + " µs</td></tr>";
  h += "</table>";
  h += "<p style=\"font-size:12px\">Durées brutes en microsecondes (alternance mark/gap) — à copier pour rejouer sur PC :</p>";
  h += "<pre id=\"durs\">" + csv + "</pre>";
  h += "<button onclick=\"cp()\">COPIER LES DURÉES</button>";
  h += "<p style=\"font-size:12px\">Dump hexadécimal :</p><pre>" + hex + "</pre>";
  h += "<div class=\"note\">Sauvegarde cette page sur ton téléphone (menu navigateur → Télécharger la page). Les durées en µs permettent de rejouer le signal sur PC (URH, rtl_433 test mode, ou un script).</div>";
  h += "<script>function cp(){var t=document.getElementById('durs').textContent;if(navigator.clipboard)navigator.clipboard.writeText(t);var b=document.querySelector('button');b.textContent='COPIÉ !';setTimeout(function(){b.textContent='COPIER LES DURÉES';},1500);}</script>";
  h += "</body></html>";
  serveur.send(200, "text/html", h);
}


// ---- Import multi-format : tableau µs, 0xHEX ou binaire (-> EV1527 pour hex/bin) ----
int ev1527FromBits(const char* bits, int nb, uint16_t* seq) {
  int n = 0;
  seq[n++] = 350; seq[n++] = 10850;              // préambule
  for (int k = 0; k < nb && n < 196; k++) {
    if (bits[k] == '1') { seq[n++] = 1050; seq[n++] = 350; }
    else                { seq[n++] = 350; seq[n++] = 1050; }
  }
  return n;
}

// ---- Bruteforce : TX d'une trame brute reçue du client (POST) ----
void gererTx() {
  String data = serveur.arg("plain");
  if (data.length() == 0) data = serveur.arg("data");
  uint16_t seq[200]; int n = 0; int i = 0;
  while (i < (int)data.length() && n < 200) {
    while (i < (int)data.length() && !isdigit((unsigned char)data.charAt(i))) i++;
    if (i >= (int)data.length()) break;
    long v = 0;
    while (i < (int)data.length() && isdigit((unsigned char)data.charAt(i))) { v = v * 10 + (data.charAt(i) - '0'); i++; }
    if (v > 0 && v < 65536) seq[n++] = (uint16_t)v;
  }
  if (n < 2) { serveur.send(200, "application/json", "{\"ok\":false,\"err\":\"vide\"}"); return; }
  envoyerSequence(seq, n);
  serveur.send(200, "application/json", "{\"ok\":true,\"n\":" + String(n) + "}");
}

void gererImporter() {
  String nom = serveur.arg("nom"); if (nom.length() == 0) nom = "import"; if (nom.length() > 27) nom = nom.substring(0, 27);
  String data = serveur.arg("data");
  uint16_t tmp[200]; int n = 0;

  String compact = "";
  for (int i = 0; i < (int)data.length(); i++) { char c = data.charAt(i); if (c == ' ' || c == ',' || c == '\n' || c == '\r' || c == '\t') continue; compact += c; }
  bool isSub = (data.indexOf("RAW_Data") >= 0 || data.indexOf("raw_data") >= 0);
  bool isHex = (data.indexOf("0x") >= 0 || data.indexOf("0X") >= 0);
  bool isBin = false;
  if (!isHex && compact.length() >= 8) {
    isBin = true;
    for (int i = 0; i < (int)compact.length(); i++) { char c = compact.charAt(i); if (c != '0' && c != '1') { isBin = false; break; } }
  }

  if (isSub) {
    // Flipper .sub : extraire les durées des lignes RAW_Data
    int p = data.indexOf("RAW_Data"); if (p < 0) p = data.indexOf("raw_data");
    String zone = data.substring(p);
    int i2 = 0;
    while (i2 < (int)zone.length() && n < 200) {
      while (i2 < (int)zone.length() && !isdigit((unsigned char)zone.charAt(i2))) i2++;
      if (i2 >= (int)zone.length()) break;
      long v = 0;
      while (i2 < (int)zone.length() && isdigit((unsigned char)zone.charAt(i2))) { v = v * 10 + (zone.charAt(i2) - '0'); i2++; }
      if (v > 0 && v < 65536) tmp[n++] = (uint16_t)v;
    }
  } else if (isHex) {
    int p = data.indexOf("0x"); if (p < 0) p = data.indexOf("0X");
    String hx = "";
    for (int i = p + 2; i < (int)data.length(); i++) { char c = data.charAt(i); if (isxdigit((unsigned char)c)) hx += c; else if (hx.length() > 0) break; }
    if (hx.length() == 0) { serveur.send(200, "application/json", "{\"ok\":false,\"err\":\"hex vide\"}"); return; }
    String bits = "";
    for (int i = 0; i < (int)hx.length(); i++) { char c = toupper(hx.charAt(i)); int v = (c >= '0' && c <= '9') ? c - '0' : c - 'A' + 10; for (int b = 3; b >= 0; b--) bits += ((v >> b) & 1) ? '1' : '0'; }
    n = ev1527FromBits(bits.c_str(), bits.length(), tmp);
  } else if (isBin) {
    n = ev1527FromBits(compact.c_str(), compact.length(), tmp);
  } else {
    // tableau µs : retirer les commentaires puis extraire les nombres
    String clean = ""; bool inLine = false, inBlock = false;
    for (int i = 0; i < (int)data.length(); i++) {
      char c = data.charAt(i);
      if (inLine) { if (c == '\n') { inLine = false; clean += ' '; } continue; }
      if (inBlock) { if (c == '*' && i + 1 < (int)data.length() && data.charAt(i + 1) == '/') { inBlock = false; i++; } continue; }
      if (c == '/' && i + 1 < (int)data.length()) { char nx = data.charAt(i + 1); if (nx == '/') { inLine = true; i++; continue; } if (nx == '*') { inBlock = true; i++; continue; } }
      clean += c;
    }
    int b1 = clean.indexOf('{'), b2 = clean.lastIndexOf('}');
    String zone = (b1 >= 0 && b2 > b1) ? clean.substring(b1 + 1, b2) : clean;
    int i2 = 0;
    while (i2 < (int)zone.length() && n < 200) {
      while (i2 < (int)zone.length() && !isdigit((unsigned char)zone.charAt(i2))) i2++;
      if (i2 >= (int)zone.length()) break;
      long v = 0;
      while (i2 < (int)zone.length() && isdigit((unsigned char)zone.charAt(i2))) { v = v * 10 + (zone.charAt(i2) - '0'); i2++; }
      if (v > 0 && v < 65536) tmp[n++] = (uint16_t)v;
    }
  }

  if (n < 4) { serveur.send(200, "application/json", "{\"ok\":false,\"err\":\"trop court\"}"); return; }
  if (nbCaptU >= MAX_CAPTURES_UTIL) { serveur.send(200, "application/json", "{\"ok\":false,\"err\":\"catalogue plein\"}"); return; }
  int idx = nbCaptU;
  nom.toCharArray(captU[idx].nom, 28);
  for (int k = 0; k < n; k++) captU[idx].seq[k] = tmp[k];
  captU[idx].nb = n;
  nbCaptU++;
  prefs.putInt("nb", nbCaptU);
  prefs.putString(("n" + String(idx)).c_str(), nom);
  String s = "";
  for (int k = 0; k < n; k++) { if (k) s += ","; s += String(captU[idx].seq[k]); }
  prefs.putString(("s" + String(idx)).c_str(), s);
  Serial.printf("[IMP] nom='%s' n=%d sub=%d hex=%d bin=%d\n", nom.c_str(), n, isSub, isHex, isBin);
  serveur.send(200, "application/json", "{\"ok\":true}");
}

void setup() {
  Serial.begin(115200);
  delay(300);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(SSID, MOT_DE_PASSE);
  WiFi.setSleep(false);  // AP jamais en veille : les téléphones restent connectés

  SPI.begin(18, 19, 23, PIN_CS);   // SPI par défaut pour notre rejeu TX

  rf.setCallback(onDecode, msgBuf, sizeof(msgBuf));
  rf.setRawPulsesCallback(onRaw);
  rf.initReceiver(PIN_DATA, 433.92);
  rf.enableReceiver();

  prefs.begin("radar433", false);
  chargerCaptures();

  serveur.on("/", []() {
    serveur.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    serveur.send(200, "text/html", PAGE);
  });
  serveur.on("/data", gererData);
  serveur.on("/catalogue", gererCatalogue);
  serveur.on("/envoyer", gererEnvoyer);
  serveur.on("/rejouer", gererRejouer);
  serveur.on("/enregistrer", gererEnregistrer);
  serveur.on("/supprimer", gererSupprimer);
  serveur.on("/capture", gererCapture);
  serveur.on("/manuel", gererManuel);
  serveur.on("/fiche", gererFiche);
  serveur.on("/reset", []() {
    serveur.send(200, "text/plain", "reboot");
    resetPending = millis();
  });
  serveur.on("/tx", HTTP_POST, gererTx);
  serveur.on("/tx", HTTP_GET, gererTx);
  serveur.on("/importer", HTTP_GET, gererImporter);
  serveur.on("/importer", HTTP_POST, gererImporter);
  serveur.on("/logo", servir_logo_raus);
  serveur.on("/slca", servir_logo_slca);
  serveur.on("/logo2", servir_logo_atari);
  serveur.begin();

  Serial.println("Réseau WiFi : " + String(SSID));
  Serial.println("Adresse : http://192.168.4.1");
}

void loop() {
  rf.loop();

  if (resetPending && millis() - resetPending > 400) ESP.restart();

  // Création différée du bloc BRUT (sur ce cœur 0, pour éviter la course de données)
  if (brutPret) {
    brutPret = false;
    Serial.printf("[BRUT] nb=%d derniereNb=%d brutT=%lu dernierT=%lu cap=%d\n", derniereNb, derniereNb, brutT, dernierT, (int)captureActive);
    static unsigned long tBrut = 0;
    if (captureActive && (millis() - tBrut) >= 800) {
      tBrut = millis();
      Serial.printf("[BRUT] n=%d d=%lu ms rssi=%d\n", derniereNb / 2, brutD, brutR);
      nbBrut++;
      Evenement e; e.no = ++numeroEvt;
      strcpy(e.modele, "BRUT");
      e.devid = derniereNb / 2;
      e.cmd = (int)(brutD / 1000);
      e.rssi = brutR; e.temp = -999; e.hum = -1;
      unsigned long now = millis();
      snprintf(e.heure, 9, "%02lu:%02lu:%02lu", (now / 3600000UL) % 24, (now / 60000UL) % 60, (now / 1000UL) % 60);
      e.dn = derniereNb < 200 ? derniereNb : 200;
      for (int k = 0; k < e.dn; k++) e.d[k] = derniereSeq[k];
      if (nbEvt < MAX_EVENTS) nbEvt++;
      for (int i = nbEvt - 1; i > 0; i--) evt[i] = evt[i - 1];
      evt[0] = e;
    }
  }

  static unsigned long tMetre = 0;
  if (millis() - tMetre >= 25) {
    tMetre = millis();
    rssiActuel = rtl_433_ESP::currentRssi;
    histoDb[histoIdx] = rssiActuel;
    histoIdx = (histoIdx + 1) % HISTO_LEN;
    hcount++;
    float inst = (float)(rssiActuel + 110) * 100.0f / 90.0f;
    if (inst > picNiv) picNiv = inst;
    else { picNiv -= 2.0f; if (picNiv < 0) picNiv = 0; }
    niveau = (int)picNiv;
  }

  static unsigned long tHb = 0;
  if (millis() - tHb >= 2000) {
    tHb = millis();
        Serial.printf("[HB] derniereNb=%d rssi=%d capture=%s brut=%d dec=%d\n", derniereNb, rssiActuel, captureActive ? "ON" : "OFF", nbBrut, nbDecode);
  }
  serveur.handleClient();
}