/*
  RADAR 433 — BOUCLE FERMÉE
  --------------------------
  L'émetteur (GPIO 5) génère une onde à 1 kHz : porteuse 433 allumée 500 µs,
  éteinte 500 µs. Le récepteur (GPIO 4) doit voir cette même cadence.

  Si on voit des transitions sur GPIO 4 au rythme de l'émission, la chaîne
  complète (émetteur + récepteur + câblage) est bonne. Sinon, on sait que le
  défaut est côté matériel, pas du côté de la télécommande.
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
  Serial.println("=== RADAR 433 : boucle fermee ===");
  Serial.println("emission 1 kHz sur GPIO 5, ecoute sur GPIO 4");
  Serial.println();
}

void loop() {
  // Émission continue à 1 kHz (500 µs porteuse, 500 µs silence).
  digitalWrite(PIN_TX, HIGH);
  delayMicroseconds(500);
  digitalWrite(PIN_TX, LOW);
  delayMicroseconds(500);

  // Toutes les 2 secondes, on échantillonne le récepteur très vite
  // et on compte les transitions : à 1 kHz on en attend ~2 par milliseconde.
  static unsigned long dernierTic = 0;
  if (millis() - dernierTic >= 2000) {
    dernierTic = millis();

    int echantillons[240];
    unsigned long t0 = micros();
    for (int i = 0; i < 240; i++) {
      echantillons[i] = digitalRead(PIN_RX);
      delayMicroseconds(50);   // 240 x 50 us = ~12 ms de fenêtre
    }
    unsigned long t1 = micros();

    int transitions = 0;
    int hauts = 0;
    for (int i = 1; i < 240; i++) {
      if (echantillons[i] != echantillons[i - 1]) transitions++;
      if (echantillons[i]) hauts++;
    }

    Serial.print("fenetre ");
    Serial.print(t1 - t0);
    Serial.print(" us  |  transitions = ");
    Serial.print(transitions);
    Serial.print("  |  niveau haut ");
    Serial.print(hauts);
    Serial.print("/240");
    Serial.println();
  }
}