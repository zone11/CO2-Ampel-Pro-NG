#include "winc_certs.h"

#include "app_globals.h"

#if WITH_CERT_CMDS

#include <spi_flash/include/spi_flash.h>
#include <string.h>

// Root certificate store in the WINC1500 flash. Layout (little endian), see scripts/winc_rootcert.py:
//   16 bytes start pattern, u32 entry count, entries:
//   20 bytes SHA-1 over the concatenated subject attribute values, start + expiry date
//   (u16 year, u8 month, day, hour, minute, second, pad), u32 key type (1 = RSA),
//   u16 modulus size, u16 exponent size, modulus, exponent, padded to 4 bytes with FF.
#define STORE_ADDR    0x4000
#define STORE_SIZE    0x1000
#define ENTRY_HEADER  44
#define MAX_ENTRIES   32
#define KEY_RSA       1

#define PEM_MAX       3072    //Base64-Text, reicht fuer RSA-4096-Roots
#define PEM_TIMEOUT   60000UL //Abbruch nach 60s ohne Eingabe

static const uint8_t START_PATTERN[16] =
{
  0x11, 0xF1, 0x12, 0xF2, 0x13, 0xF3, 0x14, 0xF4, 0x15, 0xF5, 0x16, 0xF6, 0x17, 0xF7, 0x18, 0xF8
};

typedef struct
{
  uint16_t year;
  uint8_t month, day, hour, minute, second;
} CERT_TIME;

typedef struct
{
  uint8_t hash[20];
  CERT_TIME from, to;
  const uint8_t *n;
  size_t n_len;
  const uint8_t *e;
  size_t e_len;
  char cn[48];
} CERT_INFO;

//--- SHA-1 (only for the store's name hash) ---

typedef struct
{
  uint32_t h[5];
  uint8_t block[64];
  size_t used;
  uint64_t bytes;
} SHA1_CTX;

static uint32_t rol32(uint32_t v, int s)
{
  return (v << s) | (v >> (32 - s));
}

static void sha1_block(SHA1_CTX *c)
{
  uint32_t w[80];
  for(int i = 0; i < 16; i++)
  {
    w[i] = ((uint32_t)c->block[4*i] << 24) | ((uint32_t)c->block[4*i+1] << 16) |
           ((uint32_t)c->block[4*i+2] << 8) | c->block[4*i+3];
  }
  for(int i = 16; i < 80; i++)
  {
    w[i] = rol32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
  }

  uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4];
  for(int i = 0; i < 80; i++)
  {
    uint32_t f, k;
    if(i < 20)      { f = (b & cc) | (~b & d);           k = 0x5A827999; }
    else if(i < 40) { f = b ^ cc ^ d;                    k = 0x6ED9EBA1; }
    else if(i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
    else            { f = b ^ cc ^ d;                    k = 0xCA62C1D6; }
    uint32_t t = rol32(a, 5) + f + e + k + w[i];
    e = d; d = cc; cc = rol32(b, 30); b = a; a = t;
  }
  c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}

static void sha1_init(SHA1_CTX *c)
{
  static const uint32_t init[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
  memcpy(c->h, init, sizeof(init));
  c->used = 0;
  c->bytes = 0;
}

static void sha1_update(SHA1_CTX *c, const uint8_t *data, size_t len)
{
  while(len--)
  {
    c->block[c->used++] = *data++;
    c->bytes++;
    if(c->used == 64)
    {
      sha1_block(c);
      c->used = 0;
    }
  }
}

static void sha1_final(SHA1_CTX *c, uint8_t out[20])
{
  uint64_t bits = c->bytes * 8;
  uint8_t pad = 0x80;
  sha1_update(c, &pad, 1);
  pad = 0;
  while(c->used != 56)
  {
    sha1_update(c, &pad, 1);
  }
  for(int i = 7; i >= 0; i--)
  {
    uint8_t b = (uint8_t)(bits >> (8 * i));
    sha1_update(c, &b, 1);
  }
  for(int i = 0; i < 20; i++)
  {
    out[i] = (uint8_t)(c->h[i / 4] >> (24 - 8 * (i % 4)));
  }
}

//--- Base64 + DER ---

static int b64_value(char c)
{
  if((c >= 'A') && (c <= 'Z')) { return c - 'A'; }
  if((c >= 'a') && (c <= 'z')) { return c - 'a' + 26; }
  if((c >= '0') && (c <= '9')) { return c - '0' + 52; }
  if(c == '+') { return 62; }
  if(c == '/') { return 63; }
  return -1;
}

// Decodes in place (output is shorter than input); returns the byte count, 0 on invalid input
static size_t base64_decode(char *buf, size_t len)
{
  uint32_t acc = 0;
  int bits = 0;
  size_t out = 0;

  for(size_t i = 0; i < len; i++)
  {
    if(buf[i] == '=')
    {
      break;
    }
    int v = b64_value(buf[i]);
    if(v < 0)
    {
      return 0;
    }
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    if(bits >= 8)
    {
      bits -= 8;
      ((uint8_t *)buf)[out++] = (uint8_t)(acc >> bits);
    }
  }
  return out;
}

typedef struct
{
  uint8_t tag;
  const uint8_t *val;
  size_t len;
} DER;

// Reads the element at *p (bounded by end) and advances *p past it
static bool der_next(const uint8_t **p, const uint8_t *end, DER *d)
{
  const uint8_t *q = *p;
  if((end - q) < 2)
  {
    return false;
  }
  d->tag = *q++;
  size_t len = *q++;
  if(len & 0x80)
  {
    int n = len & 0x7F;
    if((n < 1) || (n > 3) || ((end - q) < n))
    {
      return false;
    }
    for(len = 0; n > 0; n--)
    {
      len = (len << 8) | *q++;
    }
  }
  if((size_t)(end - q) < len)
  {
    return false;
  }
  d->val = q;
  d->len = len;
  *p = q + len;
  return true;
}

static bool parse_time(const DER &d, CERT_TIME *t)
{
  const uint8_t *s = d.val;
  size_t need = (d.tag == 0x17) ? 12 : (d.tag == 0x18) ? 14 : 0; //UTCTime / GeneralizedTime
  if((need == 0) || (d.len < need))
  {
    return false;
  }
  for(size_t i = 0; i < need; i++)
  {
    if((s[i] < '0') || (s[i] > '9'))
    {
      return false;
    }
  }
  #define DIGITS2(p) (((p)[0] - '0') * 10 + ((p)[1] - '0'))
  if(d.tag == 0x17)
  {
    int yy = DIGITS2(s);
    t->year = (yy < 50) ? (2000 + yy) : (1900 + yy);
    s += 2;
  }
  else
  {
    t->year = DIGITS2(s) * 100 + DIGITS2(s + 2);
    s += 4;
  }
  t->month = DIGITS2(s); t->day = DIGITS2(s + 2); t->hour = DIGITS2(s + 4);
  t->minute = DIGITS2(s + 6); t->second = DIGITS2(s + 8);
  #undef DIGITS2
  return (t->month >= 1) && (t->month <= 12) && (t->day >= 1) && (t->day <= 31);
}

// Name hash as the WINC computes it: SHA-1 over all attribute values in DER order
static bool parse_name(const DER &name, uint8_t hash[20], char *cn, size_t cn_size)
{
  static const uint8_t OID_CN[] = { 0x55, 0x04, 0x03 };
  SHA1_CTX ctx;
  const uint8_t *p = name.val, *end = name.val + name.len;

  sha1_init(&ctx);
  cn[0] = 0;
  while(p < end)
  {
    DER rdn;
    if(!der_next(&p, end, &rdn) || (rdn.tag != 0x31))
    {
      return false;
    }
    const uint8_t *q = rdn.val, *qend = rdn.val + rdn.len;
    while(q < qend)
    {
      DER atv, oid, val;
      if(!der_next(&q, qend, &atv) || (atv.tag != 0x30))
      {
        return false;
      }
      const uint8_t *r = atv.val, *rend = atv.val + atv.len;
      if(!der_next(&r, rend, &oid) || (oid.tag != 0x06) || !der_next(&r, rend, &val))
      {
        return false;
      }
      sha1_update(&ctx, val.val, val.len);
      if((oid.len == sizeof(OID_CN)) && (memcmp(oid.val, OID_CN, sizeof(OID_CN)) == 0))
      {
        size_t k = (val.len < (cn_size - 1)) ? val.len : (cn_size - 1);
        memcpy(cn, val.val, k);
        cn[k] = 0;
      }
    }
  }
  sha1_final(&ctx, hash);
  return true;
}

// Extracts what the store needs from an X.509 certificate; returns an error text or NULL
static const char *parse_cert(const uint8_t *der, size_t len, CERT_INFO *ci)
{
  static const uint8_t OID_RSA[] = { 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01 };
  static const char *invalid = "invalid certificate";
  const uint8_t *p = der, *end = der + len;
  DER cert, tbs, x, sigalg, issuer, validity, subject, spki;

  if(!der_next(&p, end, &cert) || (cert.tag != 0x30)) { return invalid; }
  p = cert.val; end = cert.val + cert.len;
  if(!der_next(&p, end, &tbs) || (tbs.tag != 0x30)) { return invalid; }
  p = tbs.val; end = tbs.val + tbs.len;
  if(!der_next(&p, end, &x)) { return invalid; }
  if((x.tag == 0xA0) && !der_next(&p, end, &x)) { return invalid; } //[0] version, then serial
  if(!der_next(&p, end, &sigalg) || !der_next(&p, end, &issuer) || !der_next(&p, end, &validity) ||
     !der_next(&p, end, &subject) || !der_next(&p, end, &spki) ||
     (validity.tag != 0x30) || (subject.tag != 0x30) || (spki.tag != 0x30))
  {
    return invalid;
  }

  const uint8_t *v = validity.val, *vend = validity.val + validity.len;
  DER t1, t2;
  if(!der_next(&v, vend, &t1) || !der_next(&v, vend, &t2) || !parse_time(t1, &ci->from) || !parse_time(t2, &ci->to))
  {
    return invalid;
  }
  if(!parse_name(subject, ci->hash, ci->cn, sizeof(ci->cn)))
  {
    return invalid;
  }

  const uint8_t *s = spki.val, *send = spki.val + spki.len;
  DER alg, bits, oid;
  if(!der_next(&s, send, &alg) || (alg.tag != 0x30) || !der_next(&s, send, &bits) || (bits.tag != 0x03))
  {
    return invalid;
  }
  const uint8_t *a = alg.val;
  if(!der_next(&a, alg.val + alg.len, &oid) || (oid.tag != 0x06))
  {
    return invalid;
  }
  if((oid.len != sizeof(OID_RSA)) || (memcmp(oid.val, OID_RSA, sizeof(OID_RSA)) != 0))
  {
    return "only RSA root certificates are supported";
  }
  if((bits.len < 1) || (bits.val[0] != 0)) { return invalid; }

  const uint8_t *k = bits.val + 1, *kend = bits.val + bits.len;
  DER seq, n, e;
  if(!der_next(&k, kend, &seq) || (seq.tag != 0x30)) { return invalid; }
  k = seq.val; kend = seq.val + seq.len;
  if(!der_next(&k, kend, &n) || (n.tag != 0x02) || !der_next(&k, kend, &e) || (e.tag != 0x02))
  {
    return invalid;
  }
  while((n.len > 1) && (n.val[0] == 0)) { n.val++; n.len--; }
  while((e.len > 1) && (e.val[0] == 0)) { e.val++; e.len--; }
  if((n.len > 512) || (e.len > 8))
  {
    return "key too large (max. RSA 4096)";
  }
  ci->n = n.val; ci->n_len = n.len;
  ci->e = e.val; ci->e_len = e.len;
  return NULL;
}

//--- store ---

static uint8_t *store = NULL;  //4 KB copy of the flash sector while a command runs
static uint32_t today = 0;     //yyyymmdd from SNTP, 0 = unknown

static uint32_t date_key(uint16_t y, uint8_t m, uint8_t d)
{
  return (uint32_t)y * 10000 + m * 100 + d;
}

static uint32_t date_key_from_epoch(unsigned long epoch) //civil-from-days (Howard Hinnant)
{
  long z = (long)(epoch / 86400) + 719468;
  long era = z / 146097;
  unsigned long doe = (unsigned long)(z - era * 146097);
  unsigned long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long y = (long)yoe + era * 400;
  unsigned long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned long mp = (5 * doy + 2) / 153;
  unsigned long d = doy - (153 * mp + 2) / 5 + 1;
  unsigned long m = (mp < 10) ? (mp + 3) : (mp - 9);
  return date_key((uint16_t)(y + (m <= 2)), (uint8_t)m, (uint8_t)d);
}

static uint32_t entry_count(void)
{
  uint32_t n;
  memcpy(&n, store + 16, sizeof(n));
  return n;
}

static size_t entry_size(const uint8_t *e)
{
  uint32_t type;
  uint16_t a, b;
  memcpy(&type, e + 36, 4);
  memcpy(&a, e + 40, 2);
  memcpy(&b, e + 42, 2);
  size_t key = (type == KEY_RSA) ? (size_t)a + b : b;
  return ENTRY_HEADER + ((key + 3) & ~(size_t)3);
}

static uint32_t entry_expiry(const uint8_t *e)
{
  uint16_t y;
  memcpy(&y, e + 28, 2);
  return date_key(y, e[30], e[31]);
}

// Offsets of all entries plus the end offset; returns the count or -1 for an unknown format
static int store_index(uint16_t *offs)
{
  if(memcmp(store, START_PATTERN, sizeof(START_PATTERN)) != 0)
  {
    return -1;
  }
  uint32_t n = entry_count();
  if(n > MAX_ENTRIES)
  {
    return -1;
  }
  size_t off = 20;
  for(uint32_t i = 0; i < n; i++)
  {
    if((off + ENTRY_HEADER) > STORE_SIZE)
    {
      return -1;
    }
    offs[i] = off;
    off += entry_size(store + off);
    if(off > STORE_SIZE)
    {
      return -1;
    }
  }
  offs[n] = off;
  return (int)n;
}

static void store_remove(uint16_t *offs, int n, int i)
{
  size_t size = offs[i + 1] - offs[i];
  memmove(store + offs[i], store + offs[i + 1], offs[n] - offs[i + 1]);
  memset(store + offs[n] - size, 0xFF, STORE_SIZE - (offs[n] - size));
  uint32_t count = n - 1;
  memcpy(store + 16, &count, sizeof(count));
}

static void print2(Print &out, unsigned int v)
{
  if(v < 10) { out.print('0'); }
  out.print(v);
}

static void print_date(Print &out, uint16_t y, uint8_t m, uint8_t d)
{
  out.print(y); out.print('-'); print2(out, m); out.print('-'); print2(out, d);
}

static void restart(Print &out)
{
  out.println("Restarting...");
  Serial.flush();
  delay(1000); //Terminal Zeit zum Lesen geben, bevor der USB-Port verschwindet
  NVIC_SystemReset();
  while(1);
}

// Stops WiFi, switches the WINC to download mode and reads the store. Restarts on failure.
static void store_open(Print &out)
{
  unsigned long now = (WiFi.status() == WL_CONNECTED) ? WiFi.getTime() : 0;
  today = (now > 0) ? date_key_from_epoch(now) : 0;

  out.println("WiFi stops, the device restarts afterwards");
  Serial.flush();
  WiFi.end();
  nm_bsp_init();
  store = (uint8_t *)malloc(STORE_SIZE);
  if((store == NULL) || (m2m_wifi_download_mode() != M2M_SUCCESS) ||
     (spi_flash_read(store, STORE_ADDR, STORE_SIZE) != M2M_SUCCESS))
  {
    out.println("ERROR: WiFi module flash not accessible");
    restart(out);
  }
  if((memcmp(store, START_PATTERN, sizeof(START_PATTERN)) != 0) && (store[0] == 0xFF) && (store[16] == 0xFF))
  {
    memset(store, 0xFF, STORE_SIZE); //geloescht (z.B. Stromausfall beim Schreiben): leer neu anlegen
    memcpy(store, START_PATTERN, sizeof(START_PATTERN));
    memset(store + 16, 0, 4);
  }
}

static bool store_write(Print &out)
{
  uint8_t check[256];
  if((spi_flash_erase(STORE_ADDR, STORE_SIZE) != M2M_SUCCESS) ||
     (spi_flash_write(store, STORE_ADDR, STORE_SIZE) != M2M_SUCCESS))
  {
    out.println("ERROR: writing the WiFi module flash failed");
    return false;
  }
  for(size_t off = 0; off < STORE_SIZE; off += sizeof(check))
  {
    if((spi_flash_read(check, STORE_ADDR + off, sizeof(check)) != M2M_SUCCESS) ||
       (memcmp(check, store + off, sizeof(check)) != 0))
    {
      out.println("ERROR: verify failed, the certificate store may be damaged");
      return false;
    }
  }
  return true;
}

static bool has_winc(Print &out)
{
  if((features & FEATURE_WINC1500) == 0)
  {
    out.println("ERROR: no WiFi module");
    return false;
  }
  return true;
}

void winc_cert_list(Print &out)
{
  uint16_t offs[MAX_ENTRIES + 1];

  if(!has_winc(out))
  {
    return;
  }
  store_open(out);
  int n = store_index(offs);
  if(n < 0)
  {
    out.println("ERROR: unknown certificate store format");
    restart(out);
  }

  out.print("Root certificates: ");
  out.print(n);
  out.print(" (");
  out.print(offs[n]);
  out.println(" of 4096 bytes used)");
  for(int i = 0; i < n; i++)
  {
    const uint8_t *e = store + offs[i];
    uint32_t type;
    uint16_t nsz, y;
    memcpy(&type, e + 36, 4);
    memcpy(&nsz, e + 40, 2);
    out.print(i < 10 ? " " : ""); out.print(i); out.print("  ");
    if(type == KEY_RSA) { out.print("RSA "); out.print(nsz * 8); }
    else                { out.print("type "); out.print(type); }
    out.print("  ");
    memcpy(&y, e + 20, 2); print_date(out, y, e[22], e[23]);
    out.print(" .. ");
    memcpy(&y, e + 28, 2); print_date(out, y, e[30], e[31]);
    out.print("  ");
    for(int k = 0; k < 4; k++) { if(e[k] < 16) { out.print('0'); } out.print(e[k], HEX); }
    if(today && (entry_expiry(e) < today)) { out.print("  EXPIRED"); }
    out.println();
  }
  out.println("OK");
  restart(out);
}

void winc_cert_remove(Print &out, unsigned int index)
{
  uint16_t offs[MAX_ENTRIES + 1];

  if(!has_winc(out))
  {
    return;
  }
  store_open(out);
  int n = store_index(offs);
  if(n < 0)
  {
    out.println("ERROR: unknown certificate store format");
    restart(out);
  }
  if(index >= (unsigned int)n)
  {
    out.println("ERROR: no such entry (see cert list)");
    restart(out);
  }
  store_remove(offs, n, index);
  if(store_write(out))
  {
    out.println("OK");
  }
  restart(out);
}

static void cert_add(Print &out, const uint8_t *der, size_t der_len)
{
  CERT_INFO ci;
  uint16_t offs[MAX_ENTRIES + 1];

  const char *err = parse_cert(der, der_len, &ci);
  if(err)
  {
    out.print("ERROR: ");
    out.println(err);
    return;
  }
  out.print("Certificate: ");
  out.print(ci.cn[0] ? ci.cn : "(no CN)");
  out.print(", RSA ");
  out.print(ci.n_len * 8);
  out.print(", valid ");
  print_date(out, ci.from.year, ci.from.month, ci.from.day);
  out.print(" .. ");
  print_date(out, ci.to.year, ci.to.month, ci.to.day);
  out.println();

  // Build the entry before the WiFi stops (der points into the PEM buffer)
  size_t key = ci.n_len + ci.e_len;
  size_t size = ENTRY_HEADER + ((key + 3) & ~(size_t)3);
  uint8_t *entry = (uint8_t *)malloc(size);
  if(entry == NULL)
  {
    out.println("ERROR: out of memory");
    return;
  }
  memset(entry, 0xFF, size);
  memcpy(entry, ci.hash, 20);
  const CERT_TIME *times[2] = { &ci.from, &ci.to };
  for(int i = 0; i < 2; i++)
  {
    uint8_t *t = entry + 20 + 8 * i;
    memcpy(t, &times[i]->year, 2);
    t[2] = times[i]->month; t[3] = times[i]->day; t[4] = times[i]->hour;
    t[5] = times[i]->minute; t[6] = times[i]->second; t[7] = 0;
  }
  uint32_t type = KEY_RSA;
  uint16_t nsz = ci.n_len, esz = ci.e_len;
  memcpy(entry + 36, &type, 4);
  memcpy(entry + 40, &nsz, 2);
  memcpy(entry + 42, &esz, 2);
  memcpy(entry + ENTRY_HEADER, ci.n, ci.n_len);
  memcpy(entry + ENTRY_HEADER + ci.n_len, ci.e, ci.e_len);

  store_open(out);
  int n = store_index(offs);
  if(n < 0)
  {
    out.println("ERROR: unknown certificate store format");
    restart(out);
  }
  for(int i = 0; i < n; i++)
  {
    if(memcmp(store + offs[i], entry, 20) == 0)
    {
      out.println("Already stored, nothing to do");
      out.println("OK");
      restart(out);
    }
  }
  if((offs[n] + size) > STORE_SIZE)
  {
    if(today == 0)
    {
      out.println("ERROR: store full and today's date unknown (remove entries with cert remove <n>)");
      restart(out);
    }
    for(int i = n - 1; i >= 0; i--) //abgelaufene Roots nur bei Platzmangel entfernen
    {
      if(entry_expiry(store + offs[i]) < today)
      {
        store_remove(offs, n, i);
        n = store_index(offs);
        out.println("Removed an expired root to make space");
      }
    }
    if((offs[n] + size) > STORE_SIZE)
    {
      out.println("ERROR: store full (remove entries with cert remove <n>)");
      restart(out);
    }
  }
  if(n >= MAX_ENTRIES)
  {
    out.println("ERROR: too many entries");
    restart(out);
  }
  memcpy(store + offs[n], entry, size);
  uint32_t count = n + 1;
  memcpy(store + 16, &count, sizeof(count));
  if(store_write(out))
  {
    out.println("OK");
  }
  restart(out);
}

//--- "cert add": PEM input ---

static char *pem = NULL;
static size_t pem_len = 0;
static bool pem_body = false;
static unsigned long pem_last = 0;

static void pem_end(void)
{
  free(pem);
  pem = NULL;
}

bool winc_cert_add_active(void)
{
  return pem != NULL;
}

void winc_cert_add_begin(Print &out)
{
  if(!has_winc(out))
  {
    return;
  }
  pem = (char *)malloc(PEM_MAX);
  if(pem == NULL)
  {
    out.println("ERROR: out of memory");
    return;
  }
  pem_len = 0;
  pem_body = false;
  pem_last = millis();
  out.println("Paste the root certificate (PEM), it ends with -----END CERTIFICATE-----");
}

void winc_cert_add_line(Print &out, const char *line)
{
  pem_last = millis();
  if(!pem_body)
  {
    if(strncmp(line, "-----BEGIN CERTIFICATE-----", 27) == 0)
    {
      pem_body = true;
    }
    else
    {
      out.println("ERROR: expected -----BEGIN CERTIFICATE-----, nothing written");
      pem_end();
    }
    return;
  }
  if(strncmp(line, "-----END CERTIFICATE-----", 25) == 0)
  {
    size_t der_len = base64_decode(pem, pem_len);
    if(der_len == 0)
    {
      out.println("ERROR: invalid certificate (base64)");
    }
    else
    {
      cert_add(out, (const uint8_t *)pem, der_len); //restarts the device if it writes
    }
    pem_end();
    return;
  }
  for(; *line; line++)
  {
    if((*line == ' ') || (*line == '\t'))
    {
      continue;
    }
    if(pem_len >= PEM_MAX)
    {
      out.println("ERROR: certificate too long, nothing written");
      pem_end();
      return;
    }
    pem[pem_len++] = *line;
  }
}

void winc_cert_add_poll(Print &out)
{
  if(pem && ((millis() - pem_last) > PEM_TIMEOUT))
  {
    out.println("ERROR: timeout, nothing written");
    pem_end();
  }
}

#endif //WITH_CERT_CMDS
