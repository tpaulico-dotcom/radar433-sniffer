import serial, time
s = serial.Serial("COM9", 115200, timeout=1)
f = open("C:/Users/Thierry/AppData/Local/Temp/tx_log.txt", "w", encoding="utf-8")
t0 = time.time()
while time.time() - t0 < 900:
    try:
        l = s.readline().decode("utf-8", "ignore")
        if l.strip():
            f.write("%6.2f %s\n" % (time.time() - t0, l.strip()))
            f.flush()
    except Exception:
        pass
s.close()
f.close()
