// Host tests: JSON parser, settings import, settings slots in a fake flash, SCD4x conversions
#include <unity.h>
#include <math.h>
#include <string>

#include "app_globals.h"
#include "calibration.h"
#include "json_parser.h"
#include "scd4x_convert.h"
#include "settings_registry.h"
#include "settings_storage.h"

// --- what the settings code needs from the rest of the firmware ---
uint8_t fake_flash[2048];
FakeNvm fake_nvm;

void FakeCtrla::operator=(uint32_t cmd)
{
  if((cmd & 0xFF) == NVMCTRL_CTRLA_CMD_ER) //the address register holds the address / 2
  {
    memset((void *)(fake_nvm.ADDR.reg * 2), 0xFF, 256);
  }
}

SETTINGS settings;
static unsigned long now_ms = 0;
unsigned long millis(void) { return now_ms; }
FakeSerial Serial;
unsigned int features = FEATURE_SCD30, remote_on = 0, co2_value = 420;
static int recals = 0, led_draws = 0, beeps = 0;
static bool recal_ok = true;
float co2_sensor(void) { return co2_value; }
bool co2_force_recalibration(void) { recals++; return recal_ok; }
void leds(uint32_t) { led_draws++; }
void led_calibration(unsigned int) { led_draws++; }
void buzzer(unsigned int on) { if(on) { beeps++; } }
static int ha_updates = 0;
void mqtt_ha_update(void) { ha_updates++; }
void co2_set_asc(bool) {}
bool apply_brightness(void *, const cfg_item_t *) { return true; }
bool apply_quiet(void *, const cfg_item_t *) { return true; }
bool apply_buzzer(void *, const cfg_item_t *) { return true; }

// --- helpers ---
class Out : public Print
{
public:
  std::string text;
  size_t write(uint8_t c) override { text += (char)c; return 1; }
};

static std::string seen;
static bool collect(void *, const char *key, json_type_t type, const char *value)
{
  seen += key; seen += '='; seen += value; seen += (type == JSON_STRING) ? 's' : (type == JSON_NUMBER) ? 'n' : (type == JSON_BOOL) ? 'b' : '0';
  seen += ';';
  return true;
}
static bool parse(const char *text)
{
  char err[48];
  seen.clear();
  return json_parse(text, collect, NULL, err, sizeof(err));
}

#define SLOT(i)   (fake_flash + ((i) * 1024))
#define HEADER    8
#define NAME_OFF  431

static uint32_t crc_of(const uint8_t *data, size_t len)
{
  uint32_t crc = 0xFFFFFFFFUL;
  while(len--)
  {
    crc ^= *data++;
    for(int i = 0; i < 8; i++) { crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1))); }
  }
  return ~crc;
}
static uint32_t slot_seq(int slot)
{
  uint32_t t[2];
  memcpy(t, SLOT(slot) + HEADER + sizeof(SETTINGS) + 4, sizeof(t));
  return (t[1] == ~t[0]) ? t[0] : 0;
}
static const char *flash_name(SETTINGS *s)
{
  settings_read(s);
  return s->name;
}
static void save_named(const char *name)
{
  SETTINGS s;
  settings_defaults(&s);
  strcpy(s.name, name);
  settings_write(&s);
}
static void set_version(int slot, uint16_t version) //keeps the record valid
{
  memcpy(SLOT(slot) + 4, &version, sizeof(version));
  uint32_t crc = crc_of(SLOT(slot), HEADER + sizeof(SETTINGS));
  memcpy(SLOT(slot) + HEADER + sizeof(SETTINGS), &crc, sizeof(crc));
}

void setUp(void)
{
  memset(fake_flash, 0xFF, sizeof(fake_flash));
  fake_nvm.INTFLAG.bit.READY = true;
  settings_defaults(&settings);
  ha_updates = 0;
}
void tearDown(void) {}

// --- JSON parser ---
void test_json_values(void)
{
  TEST_ASSERT_TRUE(parse("{\"a\":1,\"b\":\"x y\",\"c\":true,\"d\":null}"));
  TEST_ASSERT_EQUAL_STRING("a=1n;b=x ys;c=trueb;d=0;", seen.c_str());
}
void test_json_nested_and_escapes(void)
{
  TEST_ASSERT_TRUE(parse(" { \"co2\" : { \"t1\" : 600 } , \"s\" : \"a\\\"b\\u0041\\n\" } "));
  TEST_ASSERT_EQUAL_STRING("co2.t1=600n;s=a\"bA\ns;", seen.c_str());
}
void test_json_unicode(void)
{
  TEST_ASSERT_TRUE(parse("{\"a\":\"\\u00e4\\u20ac\\ud83d\\ude00\"}"));
  TEST_ASSERT_EQUAL_STRING("a=\xC3\xA4\xE2\x82\xAC\xF0\x9F\x98\x80s;", seen.c_str());
  TEST_ASSERT_TRUE(parse("{\"a\":\"\xC3\xA4\"}")); //raw UTF-8 passes through
  TEST_ASSERT_EQUAL_STRING("a=\xC3\xA4s;", seen.c_str());
  TEST_ASSERT_TRUE(parse("{\"a\":\"\\u0000\\ud83d\\ude00\\ud83d\"}")); //NUL and a lone surrogate become ?
  TEST_ASSERT_EQUAL_STRING("a=?\xF0\x9F\x98\x80?s;", seen.c_str());
  TEST_ASSERT_FALSE(parse("{\"a\":\"\\u12\"}"));
}
void test_json_errors(void)
{
  const char *bad[] = { "", "1", "[1]", "{", "{\"a\"}", "{\"a\":}", "{\"a\":1,}", "{\"a\":1} x", "{\"a\":[1]}", "{\"\":1}",
                        "{\"a\":\"x}", "{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":1}}}}}" };
  for(const char *text : bad)
  {
    TEST_ASSERT_FALSE_MESSAGE(parse(text), text);
  }
}
void test_json_limits(void)
{
  std::string key(49, 'k'), value(129, 'v');
  TEST_ASSERT_FALSE(parse(("{\"" + key + "\":1}").c_str()));
  TEST_ASSERT_FALSE(parse(("{\"a\":\"" + value + "\"}").c_str()));
  key.resize(48); value.resize(128);
  TEST_ASSERT_TRUE(parse(("{\"" + key + "\":\"" + value + "\"}").c_str()));
}

// --- settings import ---
void test_import_applies(void)
{
  Out out;
  TEST_ASSERT_TRUE(settings_json_import("{\"co2.t1\":650,\"sys.name\":\"Kitchen\"}", &out, true, true));
  TEST_ASSERT_EQUAL_UINT(650, settings.range[0]);
  TEST_ASSERT_EQUAL_STRING("Kitchen", settings.name);
}
void test_import_all_or_nothing(void)
{
  Out out;
  unsigned int t1 = settings.range[0];
  TEST_ASSERT_FALSE(settings_json_import("{\"co2.t1\":650,\"co2.t2\":99999}", &out, true, true));
  TEST_ASSERT_EQUAL_UINT(t1, settings.range[0]);
  TEST_ASSERT_NOT_NULL(strstr(out.text.c_str(), "co2.t2"));
}
void test_import_unknown_key_is_a_warning(void)
{
  Out out;
  TEST_ASSERT_TRUE(settings_json_import("{\"no.such\":1,\"co2.t1\":650}", &out, true, true));
  TEST_ASSERT_EQUAL_UINT(650, settings.range[0]);
  TEST_ASSERT_NOT_NULL(strstr(out.text.c_str(), "no.such"));
}
void test_import_mask_keeps_secret(void)
{
  Out out;
  strcpy(settings.mqtt_pass, "secret");
  TEST_ASSERT_TRUE(settings_json_import("{\"mqtt.pass\":\"***\"}", &out, true, true));
  TEST_ASSERT_EQUAL_STRING("secret", settings.mqtt_pass);
}
void test_import_remote_cannot_change_local_keys(void)
{
  Out out;
  strcpy(settings.wifi_ssid, "mine");
  TEST_ASSERT_FALSE(settings_json_import("{\"wifi.ssid\":\"other\"}", &out, false, true));
  TEST_ASSERT_EQUAL_STRING("mine", settings.wifi_ssid);
  TEST_ASSERT_TRUE(settings_json_import("{\"wifi.ssid\":\"mine\"}", &out, false, true)); //unchanged: restoring an export
}
void test_import_control_only(void)
{
  Out out;
  unsigned int t1 = settings.range[0];
  TEST_ASSERT_FALSE(settings_json_import("{\"co2.t1\":650}", &out, false, true, true));
  TEST_ASSERT_EQUAL_UINT(t1, settings.range[0]);
  TEST_ASSERT_FALSE(settings_json_import("{\"save\":true}", &out, false, true, true));
  TEST_ASSERT_TRUE(settings_json_import("{\"sys.quiet\":true}", &out, false, true, true));
  TEST_ASSERT_TRUE(settings.quiet);
}
void test_import_buzzer_old_and_new_form(void)
{
  Out out;
  TEST_ASSERT_TRUE(settings_json_import("{\"sys.buzzer\":0}", &out, true, true)); //exports up to 0.8.1
  TEST_ASSERT_FALSE(settings.buzzer);
  TEST_ASSERT_TRUE(settings_json_import("{\"sys.buzzer\":true}", &out, true, true));
  TEST_ASSERT_TRUE(settings.buzzer);
}
void test_wifi_import_local(void)
{
  Out out;
  TEST_ASSERT_TRUE(settings_json_import("{\"wifi.ssid\":\"a b=c&d\",\"wifi.pass\":\"secret\"}", &out, true, true));
  TEST_ASSERT_EQUAL_STRING("a b=c&d", settings.wifi_ssid);
  TEST_ASSERT_EQUAL_STRING("secret", settings.wifi_code);
  TEST_ASSERT_TRUE(settings_json_import("{\"wifi.pass\":\"\"}", &out, true, true)); //open network: not possible in the form
  TEST_ASSERT_EQUAL_STRING("", settings.wifi_code);
  std::string long_ssid(sizeof(settings.wifi_ssid), 'x');
  std::string json = "{\"wifi.ssid\":\"" + long_ssid + "\"}";
  TEST_ASSERT_FALSE(settings_json_import(json.c_str(), &out, true, true));
  TEST_ASSERT_EQUAL_STRING("a b=c&d", settings.wifi_ssid);
}
void test_topic_prefix(void)
{
  TEST_ASSERT_TRUE(settings_topic_valid("co2ampel"));
  TEST_ASSERT_TRUE(settings_topic_valid("a/b"));
  for(const char *bad : { "", "/a", "a/", "a//b", "a/+", "#" })
  {
    TEST_ASSERT_FALSE_MESSAGE(settings_topic_valid(bad), bad);
  }
}

// --- settings slots ---
void test_slots_alternate(void)
{
  SETTINGS s;
  TEST_ASSERT_EQUAL(SETTINGS_DEFAULTS, settings_read(&s));
  save_named("one");
  TEST_ASSERT_EQUAL_UINT32(1, slot_seq(0));
  TEST_ASSERT_EQUAL_STRING("one", flash_name(&s));
  save_named("two");
  TEST_ASSERT_EQUAL_UINT32(2, slot_seq(1));
  TEST_ASSERT_EQUAL_STRING("two", flash_name(&s));
  save_named("three");
  TEST_ASSERT_EQUAL_UINT32(3, slot_seq(0));
  TEST_ASSERT_EQUAL_UINT32(2, slot_seq(1));
  TEST_ASSERT_EQUAL_STRING("three", flash_name(&s));
  TEST_ASSERT_EQUAL(SETTINGS_LOADED, settings_read(&s));
}
void test_old_record_without_counter(void)
{
  SETTINGS s;
  save_named("old");
  memset(SLOT(0) + HEADER + sizeof(SETTINGS) + 4, 0xFF, 8);
  TEST_ASSERT_EQUAL_UINT32(0, slot_seq(0));
  TEST_ASSERT_EQUAL_STRING("old", flash_name(&s));
  save_named("new"); //goes to the free slot, the old record stays
  TEST_ASSERT_EQUAL_UINT32(1, slot_seq(1));
  TEST_ASSERT_EQUAL_STRING("old", (const char *)SLOT(0) + HEADER + NAME_OFF);
  TEST_ASSERT_EQUAL_STRING("new", flash_name(&s));
}
void test_v7_record_gets_nobuttons_default(void)
{
  SETTINGS s;
  settings_defaults(&s);
  strcpy(s.name, "v7");
  s.buzzer = false;
  uint8_t data[464]; //the v7 layout ends with name, the size-aware read must not take the next bytes for nobuttons
  memcpy(data, &s, sizeof(data));
  memset(fake_flash, 0xFF, sizeof(fake_flash));
  const uint32_t magic = 0x434F3241UL; //"CO2A"
  const uint16_t version = 7, size = sizeof(data);
  memcpy(SLOT(0), &magic, 4);
  memcpy(SLOT(0) + 4, &version, 2);
  memcpy(SLOT(0) + 6, &size, 2);
  memcpy(SLOT(0) + HEADER, data, sizeof(data));
  uint32_t crc = crc_of(SLOT(0), HEADER + sizeof(data));
  memcpy(SLOT(0) + HEADER + sizeof(data), &crc, 4);
  TEST_ASSERT_EQUAL(SETTINGS_MIGRATED, settings_read(&s));
  TEST_ASSERT_EQUAL_STRING("v7", s.name);
  TEST_ASSERT_FALSE(s.buzzer);
  TEST_ASSERT_FALSE(s.nobuttons);
}
void test_nobuttons_over_mqtt(void)
{
  Out out;
  settings.nobuttons = false;
  TEST_ASSERT_FALSE(settings_json_import("{\"sys.nobuttons\":true}", &out, false, true, true)); //mqtt.config=0
  TEST_ASSERT_FALSE(settings.nobuttons);
  TEST_ASSERT_TRUE(settings_json_import("{\"sys.nobuttons\":true}", &out, false, true)); //mqtt.config=1
  TEST_ASSERT_TRUE(settings.nobuttons);
  settings.nobuttons = false;
}
void test_bad_crc_falls_back(void)
{
  SETTINGS s;
  save_named("older");
  save_named("newest");
  SLOT(1)[200] ^= 1;
  TEST_ASSERT_EQUAL_STRING("older", flash_name(&s));
  save_named("repaired"); //the damaged slot is the one overwritten
  TEST_ASSERT_EQUAL_UINT32(2, slot_seq(1));
  TEST_ASSERT_EQUAL_STRING("repaired", flash_name(&s));
}
void test_every_cut_of_a_save_keeps_settings(void)
{
  //a power loss after any page of the save: the settings before or after it, never defaults
  for(size_t page = 0; page <= 8; page++)
  {
    SETTINGS s;
    memset(fake_flash, 0xFF, sizeof(fake_flash));
    save_named("before");
    uint8_t saved[1024];
    memcpy(saved, SLOT(1), sizeof(saved));
    save_named("after");
    memset(SLOT(1) + (page * 64), 0xFF, 1024 - (page * 64)); //the pages after the cut are still erased
    TEST_ASSERT_EQUAL(SETTINGS_LOADED, settings_read(&s));
    TEST_ASSERT_EQUAL_STRING((page == 8) ? "after" : "before", s.name);
  }
}
void test_erased_slot_a(void)
{
  SETTINGS s;
  save_named("a");
  save_named("b");
  memset(SLOT(0), 0xFF, 1024);
  TEST_ASSERT_EQUAL_STRING("b", flash_name(&s));
}
void test_both_slots_invalid(void)
{
  SETTINGS s;
  save_named("a");
  save_named("b");
  SLOT(0)[100] ^= 1;
  SLOT(1)[100] ^= 1;
  TEST_ASSERT_EQUAL(SETTINGS_DEFAULTS, settings_read(&s));
  TEST_ASSERT_EQUAL_STRING("", s.name);
}
void test_newer_record(void)
{
  SETTINGS s;
  save_named("older");
  save_named("newer");
  set_version(1, 99);
  TEST_ASSERT_EQUAL(SETTINGS_NEWER, settings_read(&s));
  TEST_ASSERT_EQUAL_STRING("", s.name); //defaults
  save_named("saved"); //a save after the downgrade goes to the other slot and wins
  TEST_ASSERT_EQUAL_STRING("newer", (const char *)SLOT(1) + HEADER + NAME_OFF);
  TEST_ASSERT_EQUAL(SETTINGS_LOADED, settings_read(&s));
  TEST_ASSERT_EQUAL_STRING("saved", s.name);
}
void test_validation_on_read(void)
{
  SETTINGS s;
  settings_defaults(&s);
  s.brightness = 999;
  s.mqtt_port = 0;
  s.range[1] = 1; //below the minimum: all thresholds back to the defaults
  strcpy(s.mqtt_topic_prefix, "a/#");
  settings_write(&s);
  SETTINGS def, r;
  settings_defaults(&def);
  TEST_ASSERT_EQUAL(SETTINGS_LOADED, settings_read(&r));
  TEST_ASSERT_EQUAL_UINT(def.brightness, r.brightness);
  TEST_ASSERT_EQUAL_UINT(def.mqtt_port, r.mqtt_port);
  TEST_ASSERT_EQUAL_UINT(def.range[1], r.range[1]);
  TEST_ASSERT_EQUAL_STRING(def.mqtt_topic_prefix, r.mqtt_topic_prefix);
}

// --- SCD4x conversions (float) against the library's double formulas ---
// --- Calibration ---
static std::string cal_text(void)
{
  char b[48];
  return calibration_status(b, sizeof(b)) ? b : "";
}
static void cal_begin(bool menu = false)
{
  recals = led_draws = beeps = 0;
  remote_on = 0;
  settings.quiet = false;
  settings.buzzer = true;
  co2_value = 420;
  calibration_start(menu);
}
static void cal_readings(int n, unsigned int a, unsigned int b)
{
  for(int i = 0; i < n; i++)
  {
    co2_value = (i & 1) ? b : a;
    calibration_reading();
  }
}
void test_cal_complete_run(void)
{
  cal_begin();
  cal_readings(35, 420, 425);
  TEST_ASSERT_TRUE(calibration_running());
  TEST_ASSERT_EQUAL(0, recals);
  TEST_ASSERT_EQUAL_STRING("running, step 35 of 36, run 1 of 3", cal_text().c_str());
  cal_readings(1, 420, 420);
  TEST_ASSERT_FALSE(calibration_running());
  TEST_ASSERT_EQUAL(1, recals);
  TEST_ASSERT_EQUAL_STRING("OK", cal_text().c_str());
  TEST_ASSERT_EQUAL(1, beeps);
  TEST_ASSERT_TRUE(calibration_leds()); //result shown for 3 s
  now_ms += 3001;
  TEST_ASSERT_FALSE(calibration_leds());
  cal_readings(40, 420, 420); //nothing happens after the end
  TEST_ASSERT_EQUAL(1, recals);
}
void test_cal_abort_never_sets_the_sensor(void)
{
  cal_begin();
  cal_readings(10, 420, 420);
  calibration_abort();
  TEST_ASSERT_FALSE(calibration_leds()); //traffic light back at once
  cal_readings(40, 420, 420);
  TEST_ASSERT_EQUAL(0, recals);
  TEST_ASSERT_EQUAL_STRING("aborted", cal_text().c_str());
}
void test_cal_unstable_gives_up_after_three_runs(void)
{
  cal_begin();
  cal_readings(1000, 420, 900);
  TEST_ASSERT_FALSE(calibration_running());
  TEST_ASSERT_EQUAL(3, recals); //applied anyway, as in the original firmware
  TEST_ASSERT_EQUAL_STRING("failed, readings not stable", cal_text().c_str());
}
void test_cal_stable_second_run_is_ok(void)
{
  cal_begin();
  cal_readings(143, 420, 900); //first run ends unstable
  TEST_ASSERT_EQUAL(1, recals);
  TEST_ASSERT_TRUE(calibration_running());
  co2_value = 420;
  cal_readings(50, 420, 420);
  TEST_ASSERT_EQUAL(2, recals);
  TEST_ASSERT_EQUAL_STRING("OK", cal_text().c_str());
}
void test_cal_leds_respect_quiet_and_enable(void)
{
  cal_begin();
  settings.quiet = true;
  led_draws = 0;
  cal_readings(36, 420, 420);
  TEST_ASSERT_EQUAL(0, led_draws);
  TEST_ASSERT_EQUAL(0, beeps);
  cal_begin();
  remote_on = 1;
  led_draws = 0;
  cal_readings(36, 420, 420);
  TEST_ASSERT_EQUAL(0, led_draws);
  remote_on = 0;
  cal_begin();
  settings.buzzer = false;
  cal_readings(36, 420, 420);
  TEST_ASSERT_EQUAL(0, beeps);
  cal_begin(true); //service menu: always
  settings.quiet = true;
  settings.buzzer = false;
  led_draws = 0;
  cal_readings(36, 420, 420);
  TEST_ASSERT_EQUAL(37, led_draws);
  TEST_ASSERT_EQUAL(1, beeps);
  settings.quiet = false;
  settings.buzzer = true;
}
void test_cal_refused_by_the_sensor(void)
{
  cal_begin();
  recal_ok = false;
  cal_readings(36, 420, 420);
  recal_ok = true;
  TEST_ASSERT_FALSE(calibration_running());
  TEST_ASSERT_EQUAL(1, recals);
  TEST_ASSERT_EQUAL_STRING("failed, sensor refused the new value", cal_text().c_str());
  TEST_ASSERT_TRUE(calibration_leds()); //result shown
  now_ms += 3001;
}
void test_cal_over_mqtt(void)
{
  Out out;
  cal_begin();
  calibration_abort();
  TEST_ASSERT_FALSE(settings_json_import("{\"calibrate\":true}", &out, false, true, true)); //mqtt.config=0
  TEST_ASSERT_FALSE(calibration_running());
  TEST_ASSERT_FALSE(settings_json_import("{\"calibrate\":1}", &out, false, true));
  TEST_ASSERT_FALSE(calibration_running());
  TEST_ASSERT_TRUE(settings_json_import("{\"calibrate\":true}", &out, true, true)); //serial, web: unknown word
  TEST_ASSERT_FALSE(calibration_running());
  TEST_ASSERT_FALSE(settings_json_import("{\"calibrate\":true,\"co2.t1\":99999}", &out, false, true)); //all or nothing
  TEST_ASSERT_FALSE(calibration_running());
  out.text.clear();
  TEST_ASSERT_TRUE(settings_json_import("{\"calibrate\":true}", &out, false, true));
  TEST_ASSERT_TRUE(calibration_running());
  TEST_ASSERT_TRUE(out.text.find("\"calibration\":\"running") != std::string::npos);
  TEST_ASSERT_FALSE(settings_json_import("{\"calibrate\":true}", &out, false, true)); //already running
  TEST_ASSERT_TRUE(calibration_running());
  TEST_ASSERT_TRUE(settings_json_import("{\"calibrate\":false}", &out, false, true));
  TEST_ASSERT_FALSE(calibration_running());
  TEST_ASSERT_EQUAL(0, recals);
  unsigned int f = features;
  features = 0;
  TEST_ASSERT_FALSE(settings_json_import("{\"calibrate\":true}", &out, false, true)); //no sensor
  features = f;
  TEST_ASSERT_FALSE(calibration_running());
}
void test_scd4x_conversions(void)
{
  for(uint32_t raw = 0; raw <= 65535; raw += 257)
  {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -45.0 + ((175.0 * raw) / 65535.0), scd4x_temperature((uint16_t)raw));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, (100.0 * raw) / 65535.0, scd4x_humidity((uint16_t)raw));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, (175.0 * raw) / 65535.0, scd4x_offset((uint16_t)raw));
  }
  TEST_ASSERT_EQUAL_UINT16(1498, scd4x_offset_raw(4.0f)); //the library's example
  TEST_ASSERT_EQUAL_UINT16(0, scd4x_offset_raw(0.0f));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_json_values);
  RUN_TEST(test_json_nested_and_escapes);
  RUN_TEST(test_json_unicode);
  RUN_TEST(test_json_errors);
  RUN_TEST(test_json_limits);
  RUN_TEST(test_import_applies);
  RUN_TEST(test_import_all_or_nothing);
  RUN_TEST(test_import_unknown_key_is_a_warning);
  RUN_TEST(test_import_mask_keeps_secret);
  RUN_TEST(test_import_remote_cannot_change_local_keys);
  RUN_TEST(test_import_control_only);
  RUN_TEST(test_import_buzzer_old_and_new_form);
  RUN_TEST(test_wifi_import_local);
  RUN_TEST(test_topic_prefix);
  RUN_TEST(test_slots_alternate);
  RUN_TEST(test_old_record_without_counter);
  RUN_TEST(test_v7_record_gets_nobuttons_default);
  RUN_TEST(test_nobuttons_over_mqtt);
  RUN_TEST(test_bad_crc_falls_back);
  RUN_TEST(test_every_cut_of_a_save_keeps_settings);
  RUN_TEST(test_erased_slot_a);
  RUN_TEST(test_both_slots_invalid);
  RUN_TEST(test_newer_record);
  RUN_TEST(test_validation_on_read);
  RUN_TEST(test_cal_complete_run);
  RUN_TEST(test_cal_abort_never_sets_the_sensor);
  RUN_TEST(test_cal_unstable_gives_up_after_three_runs);
  RUN_TEST(test_cal_stable_second_run_is_ok);
  RUN_TEST(test_cal_leds_respect_quiet_and_enable);
  RUN_TEST(test_cal_refused_by_the_sensor);
  RUN_TEST(test_cal_over_mqtt);
  RUN_TEST(test_scd4x_conversions);
  return UNITY_END();
}
