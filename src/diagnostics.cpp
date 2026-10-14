#include "diagnostics.h"

#include "app_globals.h"
#include "led_control.h"
#include "sensors.h"

#include <SPI.h>

void self_test(void) //self test
{
  //buzzer test
  buzzer(500);

  //LED test
  leds(0xFF0000); //red
  delay(1000);
  leds(0x00FF00); //green
  delay(1000);
  leds(0x0000FF); //blue
  delay(1000);
  leds(COLOR_OFF);

  //ATWINC1500 test (only if detected at boot)
  if(features & FEATURE_WINC1500)
  {
    if(WiFi.status() == WL_NO_SHIELD) //ATWINC1500 error
    {
      if(features & FEATURE_USB)
      {
        Serial.println("Error: ATWINC1500");
      }
      while(1)
      {
        leds(COLOR_RED);
        delay(500);
        leds(COLOR_YELLOW);
        delay(500);
      }
    }
    else
    {
      leds(COLOR_WHITE);
      buzzer(1000);
    }
  }

  //RFM9X test (optional, fitted?)
  SPI.begin();
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_RFM9X_CS, LOW); //RFM9X CS low/active
  SPI.transfer(0x42); //0x42 = version
  byte rfm_version = SPI.transfer(0x00);
  digitalWrite(PIN_RFM9X_CS, HIGH); //RFM9X CS high
  SPI.endTransaction();
  if(rfm_version == 0x12) //check version
  {
    leds(COLOR_WHITE);
    buzzer(1000);
  }
  if(features & FEATURE_USB)
  {
    Serial.print("RFM9X: ");
    Serial.println((rfm_version == 0x12) ? "found" : "not found");
  }

  //sensor test
  unsigned int co2, light;
  float temp, humi, pres;
  co2_value  = 0;
  temp_value = 0;
  humi_value = 0;
  pres_value = 0;
  ws2812.fill(COLOR_OFF, 0, 4);
  for(unsigned int okay=0; okay < 15;)
  {
    if(digitalRead(PIN_SWITCH) == 0) //button pressed?
    {
      break; //abort
    }
    status_led(200);

    digitalWrite(PIN_LSENSOR_PWR, HIGH); //light sensor on
    delay(50);
    light = analogRead(PIN_LSENSOR); //0...1024
    digitalWrite(PIN_LSENSOR_PWR, LOW); //light sensor off
    if((light >= 50) && (light <= 1000)) //50-1000
    {
      okay |= (1<<0);
      ws2812.setPixelColor(0, COLOR_GREEN);
    }
    else
    {
      okay &= ~(1<<0);
      ws2812.setPixelColor(0, COLOR_OFF);
    }

    if(check_sensors())
    {
      co2  = co2_sensor();
      temp = temp_sensor();
      humi = humi_sensor();
      pres = pres_sensor();

      if((co2 >= 100) && (co2 <= 1500)) //100-1500ppm
      {
        okay |= (1<<1);
        ws2812.setPixelColor(1, COLOR_BLUE);
      }
      else
      {
        okay &= ~(1<<1);
        ws2812.setPixelColor(1, COLOR_OFF);
      }

      bool has_pres = (features & (FEATURE_LPS22HB|FEATURE_BMP280)) != 0;
      if(((temp >=   5) && (temp <=   35)) && //5-35°C
         (!has_pres || ((pres >= 700) && (pres <= 1400))))   //700-1400 hPa
      {
        okay |= (1<<2);
        ws2812.setPixelColor(2, COLOR_GREEN);
      }
      else
      {
        okay &= ~(1<<2);
        ws2812.setPixelColor(2, COLOR_OFF);
      }

      if((humi >= 20) && (humi <= 80)) //20-80%
      {
        okay |= (1<<3);
        ws2812.setPixelColor(3, COLOR_GREEN);
      }
      else
      {
        okay &= ~(1<<3);
        ws2812.setPixelColor(3, COLOR_OFF);
      }

      show_data();
    }

    ws2812.show();
  }

  delay(2000);

  return;
}

void air_test(void) //fresh air test
{
  unsigned int co2;

  ws2812.fill(COLOR_WHITE, 0, 4); //LEDs white
  ws2812.show();

  while(1)
  {
    if(digitalRead(PIN_SWITCH) == 0) //button pressed?
    {
      break; //abort
    }

    status_led(200);

    if(check_sensors())
    {
      co2 = co2_sensor();

      if(co2 < 300)
      {
        ws2812.fill(COLOR_RED, 0, NUM_LEDS);
      }
      else if(co2 < 350)
      {
        ws2812.fill(COLOR_YELLOW, 0, NUM_LEDS);
      }
      else if(co2 <= 450)
      {
        ws2812.fill(COLOR_BLUE, 0, NUM_LEDS);
      }
      else if(co2 <= 500)
      {
        ws2812.fill(COLOR_YELLOW, 0, NUM_LEDS);
      }
      else //>500
      {
        ws2812.fill(COLOR_YELLOW, 0, NUM_LEDS);
      }
      ws2812.show();

      show_data();
    }
  }

  //done
  leds(COLOR_OFF);
  buzzer(250);

  return;
}
