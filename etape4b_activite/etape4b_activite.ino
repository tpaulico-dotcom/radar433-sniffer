/*
  RADAR 433 — DÉTECTEUR D'ACTIVITÉ
  --------------------------------
  Ne décode rien : signale simplement toute activité sur le récepteur.
  Si on appuie sur la sonette et que RIEN ne s'affiche, la sonette n'est
  pas en 433 MHz (probablement 868). Si "signal" s'affiche, elle est en 433
  et il faudra juste ajuster le décodage.
*/

const int PIN_RX = 4;

volatile bool activite = false;

void IRAM_ATTR surChangement() {
  activite = true;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(PIN_RX, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_RX), surChangement, CHANGE);
  Serial.println();
  Serial.println("=== DETECTEUR D'ACTIVITE ===");
  Serial.println("Appuie sur la sonette...");
}

void loop() {
  if (activite) {
    activite = false;
    Serial.println("signal 433 detecte !");
  }
  delay(5);
}