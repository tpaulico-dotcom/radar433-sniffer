#include <RadioLib.h>

// Cablage CC1101 -> ESP32:
// CSN  -> GPIO 5
// GDO0 -> GPIO 4
// SCK  -> GPIO 18
// MISO -> GPIO 19
// MOSI -> GPIO 23

#define PIN_CS    5
#define PIN_GDO0  4
#define PIN_RST   RADIOLIB_NC
#define PIN_GDO2  RADIOLIB_NC

CC1101 radio = new Module(PIN_CS, PIN_GDO0, PIN_RST, PIN_GDO2);

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n====================================");
  Serial.println("  TEST DE DETECTION CC1101 VIA SPI");
  Serial.println("====================================");

  // Initialisation radio a 433.92 MHz
  Serial.print("Initialisation CC1101 (433.92 MHz)... ");
  int state = radio.begin(433.92);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("SUCCES !");
    Serial.println(">> LE CC1101 EST DETECTE ET REPOND PARFAITEMENT ! <<");
    
    // Mesure RSSI ambiant
    float rssi = radio.getRSSI();
    Serial.print("RSSI ambiant mesure : ");
    Serial.print(rssi);
    Serial.println(" dBm");
  } else {
    Serial.print("ECHEC ! Code erreur : ");
    Serial.println(state);
    if (state == RADIOLIB_ERR_CHIP_NOT_FOUND) {
      Serial.println(">> ERREUR: Puce CC1101 non trouvee ! Verifie le cablage SPI (CS, SCK, MOSI, MISO) et l'alim 3.3V.");
    }
  }
}

void loop() {
  delay(2000);
  float rssi = radio.getRSSI();
  Serial.print("[CC1101 LIVE] RSSI: ");
  Serial.print(rssi);
  Serial.println(" dBm");
}
