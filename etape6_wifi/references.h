// Références de protocoles connus (identification des captures).

struct Reference { const char* nom; const char* protocole; int bits; };

static const Reference REFERENCES[] = {
  { "Prise Chacon — ON", "Princeton", 24 },
  { "Prise Chacon — OFF", "Princeton", 24 },
  { "Portail Nice FLO", "Nice FLO", 24 },
  { "Portail CAME TWEE", "CAME TWEE", 54 },
  { "Store Somfy Telis", "Somfy Telis", 56 },
  { "Télécommande Princeton", "Princeton", 24 },
  { "Télécommande Holtek", "Holtek", 40 },
};
static const int NB_REFERENCES = 7;
