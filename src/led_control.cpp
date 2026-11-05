#include "led_control.h"
#include "system_control.h"

void leds(uint32_t color)
{
  ws2812.fill(color, 0, NUM_LEDS);
  ws2812.show();
}

static unsigned int led_level = 0; //current level (button), 0 = led.brightness
static bool led_dark = false;

// LED state in the reset record: bit 0 = Quiet, bits 8-15 = button level
static void led_state_save(void)
{
  reset_record_led((settings.quiet ? 1 : 0) | ((led_level & 0xFF) << 8));
}

void led_state_restore(void)
{
  if(reset_unplanned())
  {
    uint32_t led = last_run()->led;
    settings.quiet = (led & 1) != 0;
    led_level = (led >> 8) & 0xFF;
  }
  led_state_save();
}

void status_wait(unsigned int ms)
{
  if(settings.quiet)
  {
    delay(ms);
  }
  else
  {
    status_led(ms);
  }
}

void led_sensor_error(void)
{
  static bool on = false;

  on = !on;
  leds((on && !settings.quiet) ? COLOR_WHITE : COLOR_OFF);
  buzzer(0);
}

void led_calibration(unsigned int co2)
{
  ws2812.fill(COLOR_WHITE, 0, 2);
  ws2812.fill((co2 <= 500) ? COLOR_BLUE : (co2 <= 750) ? COLOR_GREEN : (co2 <= 1500) ? COLOR_YELLOW : COLOR_RED, 2, 2);
  ws2812.show();
}

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
  led_state_save();
  led_brightness_update();
}

bool apply_buzzer(void *user, const cfg_item_t *item)
{
  (void)user;
  (void)item;
  if(!settings.buzzer)
  {
    buzzer(0); //immediately
  }
  return true;
}

bool apply_quiet(void *user, const cfg_item_t *item)
{
  (void)user;
  (void)item;
  led_state_save();
  if(settings.quiet)
  {
    leds(COLOR_OFF); //immediately, also with "enable"
    status_led(0);
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
  if(settings.quiet)
  {
    Serial.print("off (Quiet)");
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

  if(settings.quiet) //Quiet: LEDs and buzzer off (e.g. at night)
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
