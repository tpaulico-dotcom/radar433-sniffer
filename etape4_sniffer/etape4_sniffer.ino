/*
  RADAR 433 — SNIFFER (étape 4)
  ------------------------------
  Décode les signaux 433 MHz via la bibliothèque rc-switch.
  Affiche la valeur, le protocole, le nombre de bits et la longueur d'impulsion.

  Câblage :  récepteur DATA -> GPIO 4 (via diviseur 5V/3V3)
             émetteur  DATA -> GPIO 5
*/

#include <RCSwitch.h>

RCSwitch monSniffer = RCSwitch();

const int PIN_RX = 4;

void setup() {
  Serial.begin(115200);
  delay(300);
  monSniffer.enableReceive(PIN_RX);
  Serial.println();
  Serial.println("=== RADAR 433 : decodage ===");
  Serial.println("Appuie sur une telecommande 433 MHz...");
  Serial.println();
}

void loop() {
  if (monSniffer.available()) {
    unsigned long valeur    = monSniffer.getReceivedValue();
    unsigned int  bits      = monSniffer.getReceivedBitlength();
    unsigned int  protocole = monSniffer.getReceivedProtocol();
    unsigned int  duree     = monSniffer.getReceivedDelay();

    Serial.print("recu  valeur=");
    Serial.print(valeur);
    Serial.print("  bits=");
    Serial.print(bits);
    Serial.print("  protocole=");
    Serial.print(protocole);
    Serial.print("  impulsion=");
    Serial.print(duree);
    Serial.println(" us");

    monSniffer.resetAvailable();
  }
}