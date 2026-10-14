#include "i2c_utils.h"

static void line_low(uint8_t pin)
{
  digitalWrite(pin, LOW); //LOW first (pull-up off), then output: never drive HIGH actively
  pinMode(pin, OUTPUT);
}

static void line_release(uint8_t pin)
{
  pinMode(pin, INPUT_PULLUP);
}

bool i2c_bus_recover(uint8_t sda, uint8_t scl)
{
  line_release(sda);
  line_release(scl);
  delayMicroseconds(20);
  bool stuck = (digitalRead(sda) == LOW);

  //slave holds SDA (interrupted transfer): clock out the pending byte
  for(int i = 0; (i < 9) && (digitalRead(sda) == LOW); i++)
  {
    line_low(scl);
    delayMicroseconds(20); //approx. 25 kHz
    line_release(scl);
    delayMicroseconds(20);
  }

  //STOP: SDA rises while SCL is high
  line_low(sda);
  delayMicroseconds(20);
  line_release(sda);
  delayMicroseconds(20);

  return stuck;
}

int check_i2c(Sercom *sercom, byte addr) //1=okay
{
  int res = 0;

  for(int t=3; (t!=0) && (res==0); t--) //try 3 times
  {
    sercom->I2CM.CTRLA.bit.ENABLE = 1; //enable master mode
    delay(10); //wait 10ms
    sercom->I2CM.ADDR.bit.ADDR = (addr<<1) | 0x00; //start transfer
    delay(10); //wait 10ms
    if(sercom->I2CM.INTFLAG.bit.MB || sercom->I2CM.INTFLAG.bit.SB) //data transmitted
    {
      if(!sercom->I2CM.STATUS.bit.RXNACK) //ack received
      {
        res = 1; //ok
        break;
      }
    }
  }

  /*
  if(res == 0)
  {
    if(sercom == SERCOM1)
    {
      Wire.beginTransmission(addr);
      if(Wire.endTransmission() == 0)
      {
        res = 1; //ok
      }
    }
    else if(sercom == SERCOM2)
    {
      Wire1.beginTransmission(addr);
      if(Wire1.endTransmission() == 0)
      {
        res = 1; //ok
      }
    }
  }
  */

  return res;
}

// Reads one register; returns -1 if the device does not respond
static int i2c_read_reg(TwoWire &wire, byte addr, byte reg)
{
  wire.beginTransmission(addr);
  wire.write(reg);
  if(wire.endTransmission(false) != 0)
  {
    return -1;
  }
  if(wire.requestFrom(addr, (uint8_t)1) != 1)
  {
    return -1;
  }
  return wire.read();
}

// Known ID registers: BMP280/BME280 0xD0 (0x58/0x60), LPS22HB 0x0F (0xB1)
static int i2c_chip_id_reg(byte addr)
{
  switch(addr)
  {
    case 0x76: case 0x77: return 0xD0;
    case 0x5C: case 0x5D: return 0x0F;
    default:              return -1;
  }
}

void i2c_scan(TwoWire &wire, const char *name, Print &out)
{
  int found = 0;

  out.print(name);
  out.println(":");
  for(byte addr = 0x08; addr < 0x78; addr++)
  {
    wire.beginTransmission(addr);
    if(wire.endTransmission() != 0)
    {
      continue;
    }
    found++;
    out.print("  0x");
    if(addr < 0x10) { out.print('0'); }
    out.print(addr, HEX);

    int reg = i2c_chip_id_reg(addr);
    if(reg >= 0)
    {
      int id = i2c_read_reg(wire, addr, (byte)reg);
      out.print("  id[0x");
      out.print(reg, HEX);
      out.print("]=");
      if(id < 0) { out.print("n/a"); }
      else       { out.print("0x"); out.print(id, HEX); }
    }
    out.println();
  }
  if(found == 0)
  {
    out.println("  (none)");
  }
}
