/*
  RADAR 433 — CAPTURE BRUTE DES TIMINGS
  -------------------------------------
  Enregistre les durées exactes (en microsecondes) des impulsions HAUT/BAS
  d'une trame reçue, pour identifier le protocole de la sonette.

  La première valeur est la durée d'un niveau HAUT (présence de porteuse),
  puis on alterne BAS / HAUT. Une fin de trame = écart > 8 ms.
*/

const int PIN_RX = 4;
const int MAXP = 200;
const unsigned long FIN_TRAME = 8000;   // 8 ms de silence = fin de trame

unsigned int durees[MAXP];

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(PIN_RX, INPUT);
  Serial.println();
  Serial.println("=== CAPTURE BRUTE ===");
  Serial.println("Appuie et MAINTIENS la sonette pres du recepteur...");
}

void loop() {
  // Attendre le début d'une porteuse (niveau HAUT).
  unsigned long premier = pulseIn(PIN_RX, HIGH, 3000000UL);
  if (premier == 0) return;

  int nb = 0;
  durees[nb++] = (unsigned int)premier;

  // Alterner BAS puis HAUT jusqu'à une coupure longue.
  int etat = LOW;
  while (nb < MAXP) {
    unsigned long d = pulseIn(PIN_RX, etat, FIN_TRAME);
    if (d == 0) break;                       // coupure : fin de trame
    durees[nb++] = (unsigned int)d;
    etat = (etat == LOW) ? HIGH : LOW;
  }

  if (nb < 6) return;                        // trop court = bruit

  Serial.print("[");
  Serial.print(nb);
  Serial.print("] ");
  for (int i = 0; i < nb; i++) {
    Serial.print(durees[i]);
    Serial.print(' ');
  }
  Serial.println();
}