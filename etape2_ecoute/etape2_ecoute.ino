/*
  RADAR 433 — ÉTAPE 2 : écoute brute du récepteur
  ------------------------------------------------
  But : valider le récepteur et le câblage AVANT toute page web.
  Aucune bibliothèque. On mesure les durées des impulsions et on les affiche.

  Câblage :  Récepteur VCC->3V3  GND->GND  DATA->GPIO 4
  Sortie  :  port série à 115200 bauds

  Ce qu'on doit voir quand on appuie sur une télécommande : une rafale de
  nombres (durées en microsecondes), encadrée par REPOS. Si on ne voit rien,
  c'est le module, le fil DATA ou l'antenne — pas le programme.
*/

const int PIN_RX = 4;

const unsigned long REPOS_MIN   = 4000;   // au-delà, c'est un silence entre deux trames
const unsigned long TRAME_MAX   = 400;    // impulsions max par trame
const unsigned long ATTENTE_MAX = 300000; // 300 ms sans rien : on abandonne

unsigned int  durees[TRAME_MAX];
unsigned int  nb = 0;
unsigned long debutTrame = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(PIN_RX, INPUT);
  Serial.println();
  Serial.println("=== RADAR 433 : ecoute brute ===");
  Serial.println("Appuie sur une telecommande 433 MHz...");
  Serial.println();
}

void loop() {
  // On attend le premier front descendant (début d'une impulsion).
  unsigned long t0 = pulseIn(PIN_RX, LOW, ATTENTE_MAX);
  if (t0 == 0) return;                    // rien pendant 300 ms

  nb = 0;
  debutTrame = micros();

  // On enregistre la suite tant que les impulsions s'enchaînent sans grand repos.
  while (nb < TRAME_MAX) {
    unsigned long bas = pulseIn(PIN_RX, LOW,  REPOS_MIN);
    if (bas == 0) break;                  // repos long : trame finie
    unsigned long haut = pulseIn(PIN_RX, HIGH, REPOS_MIN);
    if (haut == 0) { haut = REPOS_MIN; }

    durees[nb++] = (unsigned int)bas;
    if (nb < TRAME_MAX) durees[nb++] = (unsigned int)haut;
  }

  if (nb < 4) return;                     // trop court : du bruit

  // Durée totale de la trame
  Serial.print("REPOS ");
  Serial.print(micros() - debutTrame);
  Serial.print(" us  |  ");
  Serial.print(nb);
  Serial.print(" impulsions  |  ");
  Serial.print(durees[0]);
  for (unsigned int i = 1; i < nb && i < 80; i++) {
    Serial.print(' ');
    Serial.print(durees[i]);
  }
  Serial.println();
}
