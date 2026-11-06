/*
  RADAR 433 — TEST DIFFÉRENTIEL PORTEUSE ON/OFF
  ----------------------------------------------
  Pendant 3 s : émetteur = porteuse continue (GPIO 5 à fond).
  Pendant 3 s : émetteur = silence.
  On compte à chaque phase combien de fois GPIO 4 est HAUT.

  Si tout marche, les deux compteurs DIFFÈRENT franchement (la porteuse
  change l'état du récepteur). S'ils sont identiques, la chaîne est coupée
  (émetteur muet, récepteur sourd, ou un fil sur la mauvaise broche).
*/

const int PIN_TX = 5;
const int PIN_RX = 4;

void setup() {
  Serial.begin(115200);
  delay(400);
  pinMode(PIN_TX, OUTPUT);
  pinMode(PIN_RX, INPUT);
  digitalWrite(PIN_TX, LOW);
  Serial.println();
  Serial.println("=== TEST PORTEUSE ON/OFF ===");
}

// Compte les niveaux HAUT sur GPIO 4 pendant duree ms.
int compterHauts(unsigned long duree) {
  int hauts = 0;
  int total = 0;
  unsigned long fin = millis() + duree;
  while (millis() < fin) {
    if (digitalRead(PIN_RX)) hauts++;
    total++;
  }
  // renvoie en pourcentage pour que ce soit lisible
  if (total == 0) return 0;
  return (hauts * 100) / total;
}

void loop() {
  // Phase 1 : porteuse continue
  digitalWrite(PIN_TX, HIGH);
  int pourcent_on = compterHauts(3000);

  // Phase 2 : silence
  digitalWrite(PIN_TX, LOW);
  int pourcent_off = compterHauts(3000);

  Serial.print("porteuse ON  : niveau haut ");
  Serial.print(pourcent_on);
  Serial.println("%");
  Serial.print("porteuse OFF : niveau haut ");
  Serial.print(pourcent_off);
  Serial.println("%");
  Serial.println("--------------------");
}