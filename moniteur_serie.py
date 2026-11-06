import serial, time, sys

PORT = "COM9"
DEBIT = 115200

print("=" * 52)
print("   MONITEUR SERIE  -  RADAR 433 / SNIFFER RAUS")
print("=" * 52)
print("Port :", PORT, "| Vitesse :", DEBIT)
print("Ferme cette fenetre pour arreter.")
print()

while True:
    try:
        s = serial.Serial(PORT, DEBIT, timeout=1)
        break
    except Exception as e:
        print("Impossible d'ouvrir", PORT, ":", e)
        print("Verifie que le cable USB est branche. Nouvelle tentative dans 2 s...")
        time.sleep(2)

lignes = 0
while True:
    try:
        l = s.readline().decode("utf-8", errors="ignore").rstrip()
        if l and not l.startswith(("rst:", "configsip", "clk_drv", "mode:", "load:", "entry", "ho 0", "ets ")):
            print(time.strftime("%H:%M:%S  ") + l, flush=True)
            lignes += 1
    except KeyboardInterrupt:
        break
    except Exception as e:
        print("Erreur lecture :", e)
        time.sleep(1)
