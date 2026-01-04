#include "diagnostics.h"

#include "app_globals.h"
#include "led_control.h"
#include "sensors.h"

#include <SPI.h>

void self_test(void) //Testprogramm
{
  //Buzzer-Test
  buzzer(500); //500ms Buzzer an

  //LED-Test
  leds(0xFF0000); //LEDs rot
  delay(1000); //1s warten
  leds(0x00FF00); //LEDs gruen
  delay(1000); //1s warten
  leds(0x0000FF); //LEDs blau
  delay(1000); //1s warten
  leds(FARBE_AUS); //LEDs aus

  #if WIFI_AMPEL
    //ATWINC1500-Test
    if(WiFi.status() == WL_NO_SHIELD) //ATWINC1500 Fehler
    {
      if(features & FEATURE_USB)
      {
        Serial.println("Error: ATWINC1500");
      }
      while(1)
      {
        leds(COLOR_RED); //LEDs rot
        delay(500); //500ms warten
        leds(COLOR_YELLOW); //LEDs gelb
        delay(500); //500ms warten
      }
    }
    else
    {
      leds(FARBE_WEISS); //LEDs weiss
      buzzer(1000); //1s Buzzer an
    }
  #endif

  //RFM9X-Test
  #if PRO_AMPEL
    SPI.begin();
    SPI.setDataMode(SPI_MODE0);
    SPI.setBitOrder(MSBFIRST);
    SPI.setClockDivider(SPI_CLOCK_DIV128);
    digitalWrite(20, LOW); //RFM9X CS low/active
    SPI.transfer(0x42); //0x42 = version
    byte i = SPI.transfer(0x00);
    digitalWrite(20, HIGH); //RFM9X CS high
    if(i == 0x12) //check version
    {
      leds(FARBE_WEISS); //LEDs weiss
      buzzer(1000); //1s Buzzer an
    }
  #endif

  //Sensor-Test
  unsigned int co2, light;
  float temp, humi, pres;
  co2_value  = 0;
  temp_value = 0;
  humi_value = 0;
  #if PRO_AMPEL
    pres_value = 0;
  #endif
  ws2812.fill(FARBE_AUS, 0, 4); //LEDs aus
  for(unsigned int okay=0; okay < 15;)
  {
    if(digitalRead(PIN_SWITCH) == 0) //Taster gedrueckt?
    {
      break; //Abbruch
    }
    status_led(200); //Status-LED

    digitalWrite(PIN_LSENSOR_PWR, HIGH); //Lichtsensor an
    delay(50); //50ms warten
    light = analogRead(PIN_LSENSOR); //0...1024
    digitalWrite(PIN_LSENSOR_PWR, LOW); //Lichtsensor aus
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

      if(((temp >=   5) && (temp <=   35)) && //5-35°C
         ((pres >= 700) && (pres <= 1400)))   //700-1400 hPa
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

  delay(2000); //2s warten

  return;
}

void air_test(void) //Frischluft-Test
{
  unsigned int co2;

  ws2812.fill(COLOR_WHITE, 0, 4); //LEDs weiss
  ws2812.show();

  while(1)
  {
    if(digitalRead(PIN_SWITCH) == 0) //Taster gedrueckt?
    {
      break; //Abbruch
    }

    status_led(200); //Status-LED

    if(check_sensors())
    {
      co2 = co2_sensor();

      if(co2 < 300)
      {
        ws2812.fill(COLOR_RED, 0, NUM_LEDS); //rot
      }
      else if(co2 < 350)
      {
        ws2812.fill(COLOR_YELLOW, 0, NUM_LEDS); //gelb
      }
      else if(co2 <= 450)
      {
        ws2812.fill(COLOR_BLUE, 0, NUM_LEDS); //blau
      }
      else if(co2 <= 500)
      {
        ws2812.fill(COLOR_YELLOW, 0, NUM_LEDS); //gelb
      }
      else //>500
      {
        ws2812.fill(COLOR_YELLOW, 0, NUM_LEDS); //rot
      }
      ws2812.show();

      show_data();
    }
  }

  //Ende
  leds(COLOR_OFF);//LEDs aus
  buzzer(250); //250ms Buzzer an

  return;
}
