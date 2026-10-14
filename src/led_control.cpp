#include "led_control.h"

void leds(uint32_t color)
{
  ws2812.fill(color, 0, NUM_LEDS);
  ws2812.show();
}

static unsigned int led_level = 0; //aktuelle Stufe (Taster), 0 = led.brightness
static bool led_dark = false;

void led_brightness_update(void)
{
  unsigned int level = (led_level > 0) ? led_level : settings.brightness;

  if(remote_on)
  {
    level = HELLIGKEIT_REMOTE;
  }
  else if(settings.led_auto && led_dark && (settings.brightness_dark < level))
  {
    level = settings.brightness_dark;
  }
  ws2812.setBrightness(level);
}

void led_set_dark(bool dark)
{
  led_dark = dark;
  led_brightness_update();
}

void led_button_step(void)
{
  unsigned int level = ((led_level > 0) ? led_level : settings.brightness) / 2;
  unsigned int min = settings.led_auto ? settings.brightness_dark : 1;

  led_level = ((level < min) || (level < 1)) ? 0 : level; //unter dem Minimum: zurueck zum Maximum
  led_brightness_update();
}

void print_led_status(void)
{
  Serial.print("LEDs: ");
  if(settings.led_disable)
  {
    Serial.println("off (led.disable)");
    return;
  }
  Serial.print("brightness ");
  Serial.print(ws2812.getBrightness());
  Serial.print(led_dark ? " (dark" : " (light");
  Serial.println(settings.led_auto ? ", auto)" : ", fixed)");
}

bool apply_brightness(void *user, const cfg_item_t *item)
{
  (void)user;
  (void)item;
  led_level = 0;
  led_brightness_update();
  ws2812.show();
  return true;
}

void status_led(unsigned int on)
{
  if(on == 0)
  {
    digitalWrite(PIN_LED, LOW); //Status-LED aus
  }
  else if(on == 1)
  {
    digitalWrite(PIN_LED, HIGH); //Status-LED an
  }
  else if(on < 2000)
  {
    on = on/2;
    digitalWrite(PIN_LED, HIGH); //Status-LED an
    delay(on); //ms warten
    digitalWrite(PIN_LED, LOW); //Status-LED aus
    delay(on); //ms warten
  }

  return;
}

void buzzer(unsigned int on)
{
  if(on == 0)
  {
    analogWrite(PIN_BUZZER, 0); //Buzzer aus
  }
  else if(on == 1)
  {
    analogWrite(PIN_BUZZER, 255/2); //Buzzer an
  }
  else if(on < 2000)
  {
    analogWrite(PIN_BUZZER, 255/2); //Buzzer an
    delay(on); //ms warten
    analogWrite(PIN_BUZZER, 0); //Buzzer aus
  }

  return;
}

void ampel(unsigned int co2)
{
  static unsigned int blinken=0;

  if(settings.led_disable) //LEDs und Buzzer aus (z.B. nachts)
  {
    leds(FARBE_AUS);
    buzzer(0);
    return;
  }

  //LEDs
  if(co2 < settings.range[0]) //blau (very fresh air)
  {
    blinken = 0;
    leds(settings.color_t1);
  }
  else if(co2 < settings.range[1]) //gruen (good)
  {
    blinken = 0;
    leds(settings.color_t2);
  }
  else if(co2 < settings.range[2]) //gelb (warning)
  {
    blinken = 0;
    leds(settings.color_t3);
  }
  else if(co2 < settings.range[3]) //rot (alert)
  {
    blinken = 0;
    leds(settings.color_t4);
  }
  else //rot blinken (critical - blinking)
  {
    if(blinken == 0)
    {
      leds(ws2812.Color(10,0,0)); //rot schwache Helligkeit
    }
    else
    {
      leds(settings.color_t4); //rot
    }
    blinken = 1-blinken; //invertieren
  }

  //Buzzer
  if(co2 < settings.range[4])
  {
    buzzer(0); //Buzzer aus
  }
  else
  {
    if((blinken == 0) && (buzzer_timer == 0) && settings.buzzer)
    {
      buzzer(1); //Buzzer an
    }
    else
    {
      buzzer(0); //Buzzer aus
    }
  }

  return;
}
