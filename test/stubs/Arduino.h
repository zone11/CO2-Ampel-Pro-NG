// Just enough of Arduino for the settings code on the host
#ifndef ARDUINO_H_STUB
#define ARDUINO_H_STUB

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

typedef bool boolean;
typedef uint8_t byte;
#define HEX 16
#define DEC 10

class Print
{
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  size_t print(const char *s) { size_t n = 0; while(*s) { n += write((uint8_t)*s++); } return n; }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(unsigned long v, int base = DEC) { char b[24]; snprintf(b, sizeof(b), (base == HEX) ? "%lX" : "%lu", v); return print(b); }
  size_t print(long v) { char b[24]; snprintf(b, sizeof(b), "%ld", v); return print(b); }
  size_t print(unsigned int v, int base = DEC) { return print((unsigned long)v, base); }
  size_t print(int v) { return print((long)v); }
  size_t print(unsigned char v, int base = DEC) { return print((unsigned long)v, base); }
  size_t print(unsigned short v, int base = DEC) { return print((unsigned long)v, base); }
  size_t println(void) { return print("\r\n"); }
  template<typename T> size_t println(T v) { size_t n = print(v); return n + println(); }
};

class Stream : public Print {};

class IPAddress
{
public:
  IPAddress() { memset(b, 0, sizeof(b)); }
  IPAddress(uint8_t a, uint8_t c, uint8_t d, uint8_t e) { b[0] = a; b[1] = c; b[2] = d; b[3] = e; }
  uint8_t operator[](int i) const { return b[i]; }
  uint8_t &operator[](int i) { return b[i]; }
private:
  uint8_t b[4];
};

unsigned long millis(void);

// Fake flash for src/settings_storage.cpp
extern uint8_t fake_flash[2048];
#define SETTINGS_FLASH_ADDR ((uintptr_t)fake_flash)

#define __disable_irq()
#define __enable_irq()
#define NVMCTRL_CTRLA_CMDEX_KEY 0xA500u
#define NVMCTRL_CTRLA_CMD_ER    0x02u
#define NVMCTRL_CTRLA_CMD_WP    0x04u
#define NVMCTRL_CTRLA_CMD_PBC   0x44u

struct FakeCtrla
{
  void operator=(uint32_t cmd);
};
struct FakeNvm
{
  struct { uintptr_t reg; } ADDR;
  struct { FakeCtrla reg; } CTRLA;
  struct { struct { bool READY; } bit; } INTFLAG;
};
extern FakeNvm fake_nvm;
#define NVMCTRL (&fake_nvm)

#endif
