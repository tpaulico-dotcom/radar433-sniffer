#!/bin/bash
A="C:/Users/Thierry/AppData/Local/Temp/acli/arduino-cli.exe"
T="C:/Users/Thierry/Desktop/radar433/etape8_rtl433"
F="-DRF_CC1101 -DRF_MODULE_CS=5 -DRF_MODULE_GDO0=4 -DRF_MODULE_GDO2=-1 -DCC1101_RX_BANDWIDTH=203.125 -DCC1101_AGCCTRL0=0x93 -DCC1101_OOK_TUNING"
"$A" compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app --build-property compiler.cpp.extra_flags="$F" --build-property compiler.c.extra_flags="$F" "$T"
CP=$?
echo "COMPILE_EXIT=$CP"
if [ $CP -eq 0 ]; then
  "$A" upload -p COM9 --fqbn esp32:esp32:esp32:PartitionScheme=huge_app "$T"
  echo "UPLOAD_EXIT=$?"
fi
