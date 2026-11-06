#include <rtl_433_ESP.h>
rtl_433_ESP rf;
char buf[1024];

void message(char* m){ Serial.print("DECODE: "); Serial.println(m); }

void raw(const int* p, const int* g, unsigned int n, unsigned long d, int r){
  Serial.printf("RAW: %u imp, %lu us, rssi %d | M:", n, d, r);
  for(unsigned int i=0;i<n && i<12;i++) Serial.printf(" %d", p[i]);
  Serial.print(" | G:");
  for(unsigned int i=0;i<n && i<12;i++) Serial.printf(" %d", g[i]);
  Serial.println();
}

void setup(){
  Serial.begin(115200);
  rf.setCallback(message, buf, sizeof(buf));
  rf.setRawPulsesCallback(raw);
  rf.initReceiver(4, 433.92);
  rf.enableReceiver();
  Serial.println("READY");
}

void loop(){ rf.loop(); }