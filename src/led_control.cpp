#include "led_control.h"

void leds(uint32_t color)
{
  ws2812.fill(color, 0, NUM_LEDS);
  ws2812.show();
}

static unsigned int led_level = 0; //current level (button), 0 = led.brightness
static bool led_dark = false;

void led_brightness_update(void)
{
  unsigned int level = (led_level > 0) ? led_level : settings.brightness;

  if(remote_on)
  {
    level = BRIGHTNESS_MENU;
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

  led_level = ((level < min) || (level < 1)) ? 0 : level; //below the minimum: back to the maximum
  led_brightness_update();
}

bool apply_led_disable(void *user, const cfg_item_t *item)
{
  (void)user;
  (void)item;
  if(settings.led_disable)
  {
    leds(COLOR_OFF); //immediately, also with "enable"
    buzzer(0);
  }
  else if(remote_on)
  {
    leds(COLOR_MENU); //restore the "enable" indicator; otherwise traffic_light() takes over on the next tick
  }
  return true;
}

void print_led_status(void)
{
  uint32_t shown = ws2812.getPixelColor(0); //actually shown (scaled by the brightness)

  Serial.print("LEDs: ");
  if(settings.led_disable)
  {
    Serial.print("off (led.disable)");
  }
  else
  {
    Serial.print("brightness ");
    Serial.print(ws2812.getBrightness());
    Serial.print(led_dark ? " (dark" : " (light");
    Serial.print(settings.led_auto ? ", auto)" : ", fixed)");
  }
  Serial.print(", showing ");
  if(shown == 0)
  {
    Serial.println("nothing");
  }
  else
  {
    char hex[8];
    snprintf(hex, sizeof(hex), "%06lX", (unsigned long)shown);
    Serial.println(hex);
  }
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
    digitalWrite(PIN_LED, LOW);
  }
  else if(on == 1)
  {
    digitalWrite(PIN_LED, HIGH);
  }
  else if(on < 2000)
  {
    on = on/2;
    digitalWrite(PIN_LED, HIGH);
    delay(on);
    digitalWrite(PIN_LED, LOW);
    delay(on);
  }

  return;
}

void buzzer(unsigned int on)
{
  if(on == 0)
  {
    analogWrite(PIN_BUZZER, 0);
  }
  else if(on == 1)
  {
    analogWrite(PIN_BUZZER, 255/2);
  }
  else if(on < 2000)
  {
    analogWrite(PIN_BUZZER, 255/2);
    delay(on);
    analogWrite(PIN_BUZZER, 0);
  }

  return;
}

void traffic_light(unsigned int co2)
{
  static unsigned int blink_on=0;

  if(settings.led_disable) //LEDs and buzzer off (e.g. at night)
  {
    leds(COLOR_OFF);
    buzzer(0);
    return;
  }

  //LEDs
  if(co2 < settings.range[0]) //blue (very fresh air)
  {
    blink_on = 0;
    leds(settings.color_t1);
  }
  else if(co2 < settings.range[1]) //green (good)
  {
    blink_on = 0;
    leds(settings.color_t2);
  }
  else if(co2 < settings.range[2]) //yellow (warning)
  {
    blink_on = 0;
    leds(settings.color_t3);
  }
  else if(co2 < settings.range[3]) //red (alert)
  {
    blink_on = 0;
    leds(settings.color_t4);
  }
  else //blinking red (critical)
  {
    if(blink_on == 0)
    {
      leds(ws2812.Color(10,0,0)); //dim red
    }
    else
    {
      leds(settings.color_t4); //red
    }
    blink_on = 1-blink_on;
  }

  //buzzer
  if(co2 < settings.range[4])
  {
    buzzer(0);
  }
  else
  {
    if((blink_on == 0) && (buzzer_timer == 0) && settings.buzzer)
    {
      buzzer(1);
    }
    else
    {
      buzzer(0);
    }
  }

  return;
}
