# RADAR 433 — Sniffer / Rejeu 433 MHz (ESP32 + CC1101)

Un **sniffer et analyseur 433 MHz** DIY basé sur un ESP32 et un module CC1101, avec une interface web embarquée (thème DOS / Atari), le décodage professionnel via `rtl_433_ESP`, la capture brute, le rejeu, et un moniteur système.

![Statut](https://img.shields.io/badge/status-fonctionnel-brightgreen) ![Plateforme](https://img.shields.io/badge/plateforme-ESP32-blue) ![Licence](https://img.shields.io/badge/licence-MIT-green)

---

## ✨ Fonctionnalités

- **Réception + décodage** de ~328 protocoles via [rtl_433_ESP](https://github.com/.../rtl_433_ESP) (sonnettes EV1527, télécommandes, capteurs météo…)
- **Capture BRUT** : mémorise la trame brute même si elle n'est pas décodée (clés de voiture, codes tournants…)
- **Rejeu** (TX) d'une trame capturée ou importée, avec retour RX automatique
- **Moniteur système** : flux hexadécimal continu (seuil → `0000`), logs, trames reconnues surlignées en jaune fluo
- **Catalogue** : signaux sauvegardés, classés, réutilisables
- **Import multi-format** : tableau C en µs, hex (`0x…`), binaire, et **fichiers `.sub` de Flipper** (`RAW_Data`)
- **Fiche HTML autonome** par capture (sauvegardable sur téléphone, réutilisable sur PC)
- **Thèmes** : DOS (bleu) et Atari ST (vert acide)
- **Interface WiFi** : le module crée son propre point d'accès `Radar433` → `http://192.168.4.1`

## 🔌 Câblage (CC1101 → ESP32)

| CC1101 | ESP32 |
|--------|-------|
| VCC | **3V3** (⚠️ jamais 5 V) |
| GND | GND |
| CSN (CS) | GPIO **5** |
| GDO0 | GPIO **4** |
| GDO2 | (non connecté) |
| SCK | GPIO **18** |
| MOSI | GPIO **23** |
| MISO | GPIO **19** |

> ⚠️ Toujours visser l'antenne SMA **avant** d'alimenter, et **ne jamais émettre sans antenne**.

## 🛠️ Compilation / flash

- **IDE** : Arduino IDE (ou `arduino-cli`)
- **Carte** : `ESP32 Dev Module`
- **Schéma de partition** : `Huge APP (3MB No OTA/1MB SPIFFS)`
- **Bibliothèques** : `rtl_433_ESP`, `RadioLib`
- **Flags de compilation** :
  ```
  -DRF_CC1101 -DRF_MODULE_CS=5 -DRF_MODULE_GDO0=4 -DRF_MODULE_GDO2=-1
  -DCC1101_RX_BANDWIDTH=203.125 -DCC1101_AGCCTRL0=0x93
  ```

## 🚀 Utilisation

1. Flasher `etape8_rtl433/etape8_rtl433.ino`
2. Se connecter au WiFi **`Radar433`**
3. Ouvrir **`http://192.168.4.1`**
4. `START` pour écouter, appuyer sur une télécommande, toucher la ligne pour enregistrer, `▶` pour rejouer.

## 📁 Structure

```
etape8_rtl433/      → croquis final (firmware + page web)
etape7_cc1101/      → version pré-rtl_433 (référence)
sauvegardes/        → (ignoré) snapshots locaux
```

## ⚠️ Avertissement légal

> Cet outil émet et rejoue des signaux radio 433 MHz. **Utilisez-le uniquement sur vos propres appareils, ou avec l'accord explicite du propriétaire.** Le rejeu de signaux d'appareils que vous ne possédez pas peut être illégal selon votre pays et constitue une atteinte à la vie privée / à la sécurité. L'auteur décline toute responsabilité en cas d'usage abusif.

## 📜 Licence

MIT — voir [LICENSE](LICENSE).
