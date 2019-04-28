#define CONFIG_ARDUHAL_LOG_DEFAULT_LEVEL 3
#include <M5Stack.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "nvs_flash.h"

#include "hap.h"

#define TAG "HUE"

#define ACCESSORY_NAME "LIGHT BULB"
#define MANUFACTURER_NAME "WinDesign"
#define MODEL_NAME "ESP32_ACC"
#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))
#define ACCESSORY_CODE "053-58-201"

#if 1
#define EXAMPLE_ESP_WIFI_SSID "hoge"
#define EXAMPLE_ESP_WIFI_PASS "1234"
#endif
#if 0
#define EXAMPLE_ESP_WIFI_SSID "NO_RUN"
#define EXAMPLE_ESP_WIFI_PASS "1qaz2wsx"
#endif

// static gpio_num_t LED_PORT = GPIO_NUM_2;

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

uint32_t toRgb(int hue, int sat, int bri)
{
  float h = hue / 360.0;
  float s = sat / 100.0;
  float v = bri / 100.0;
  float r = v;
  float g = v;
  float b = v;
  if (s > 0.0f)
  {
    h *= 6.0f;
    int i = (int)h;
    float f = h - (float)i;
    switch (i)
    {
    default:
    case 0:
      g *= 1 - s * (1 - f);
      b *= 1 - s;
      break;
    case 1:
      r *= 1 - s * f;
      b *= 1 - s;
      break;
    case 2:
      r *= 1 - s;
      b *= 1 - s * (1 - f);
      break;
    case 3:
      r *= 1 - s;
      g *= 1 - s * f;
      break;
    case 4:
      r *= 1 - s * (1 - f);
      g *= 1 - s;
      break;
    case 5:
      g *= 1 - s;
      b *= 1 - s * f;
      break;
    }
  }
  uint32_t color = (int)(r * 31) << 11 | (int)(g * 0x3f) << 5 | (int)(b * 0x1f);
  printf("[MAIN] (%.2f,%.2f,%.2f) -> %d\n", h, s, v, color);

  return color;
}

static int hue = 0;        // 色相
static int bright = 0;     // 輝度
static int saturation = 0; // 彩度

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
static void *a;
static void *_ev_handle;
static boolean led = false; // on/off
void *led_read(void *arg)
{
  printf("[MAIN] LED READ\n");
  return (void *)led;
}
void led_write(void *arg, void *value, int len)
{
  // led = *((boolean *)value);
  if (value)
    led = true;
  else
    led = false;

  printf("[MAIN] LED WRITE. %d(len=%d)\n", led, len);

  if (led)
  {
    M5.Lcd.clear(toRgb(hue, saturation, bright));
    M5.Lcd.setBrightness((uint8_t)(bright * 255 / 100));
  }
  else
  {
    M5.Lcd.clear(BLACK);
    M5.Lcd.setBrightness(0);
  }

  if (_ev_handle)
    hap_event_response(a, _ev_handle, (void *)led);

  return;
}
void led_notify(void *arg, void *ev_handle, bool enable)
{
  if (enable)
  {
    _ev_handle = ev_handle;
  }
  else
  {
    _ev_handle = NULL;
  }
}

static void *_ev_handle_hue;
void *hue_read(void *arg)
{
  printf("[MAIN] HUE READ\n");
  return (void *)hue;
}
void hue_write(void *arg, void *value, int len)
{
  hue = (int)value;
  printf("[MAIN] HUE WRITE. %d(len=%d)\n", hue, len);

  M5.Lcd.clear(toRgb(hue, saturation, bright));
  if (_ev_handle_hue)
    hap_event_response(a, _ev_handle_hue, (void *)hue);

  return;
}
void hue_notify(void *arg, void *ev_handle, bool enable)
{
  if (enable)
  {
    _ev_handle_hue = ev_handle;
  }
  else
  {
    _ev_handle_hue = NULL;
  }
}

static void *_ev_handle_bright;
void *bri_read(void *arg)
{
  printf("[MAIN] BRIGHTNESS READ\n");
  return (void *)bright;
}
void bri_write(void *arg, void *value, int len)
{
  bright = (int)value;
  printf("[MAIN] BRIGHTNESS WRITE. %d->%d(len=%d)\n", bright, (uint8_t)(bright * 255 / 100), len);

  M5.Lcd.clear(toRgb(hue, saturation, bright));
  M5.Lcd.setBrightness((uint8_t)(bright * 255 / 100));

  if (_ev_handle_bright)
    hap_event_response(a, _ev_handle_bright, (void *)bright);

  return;
}
void bri_notify(void *arg, void *ev_handle, bool enable)
{
  if (enable)
  {
    _ev_handle_bright = ev_handle;
  }
  else
  {
    _ev_handle_bright = NULL;
  }
}

static void *_ev_handle_saturation;
void *sat_read(void *arg)
{
  printf("[MAIN] SATURATION READ\n");
  return (void *)saturation;
}
void sat_write(void *arg, void *value, int len)
{
  saturation = (int)value;
  printf("[MAIN] SATURATION WRITE. %d(len=%d)\n", saturation, len);

  M5.Lcd.clear(toRgb(hue, saturation, bright));
  if (_ev_handle_saturation)
    hap_event_response(a, _ev_handle_saturation, (void *)saturation);

  return;
}
void sat_notify(void *arg, void *ev_handle, bool enable)
{
  if (enable)
  {
    _ev_handle_saturation = ev_handle;
  }
  else
  {
    _ev_handle_saturation = NULL;
  }
}

static bool _identifed = false;
void *identify_read(void *arg)
{
  return (void *)true;
}

void hap_object_init(void *arg)
{
  void *accessory_object = hap_accessory_add(a);
  struct hap_characteristic cs[] = {
      {HAP_CHARACTER_IDENTIFY, (void *)true, NULL, identify_read, NULL, NULL},
      {HAP_CHARACTER_MANUFACTURER, (void *)MANUFACTURER_NAME, NULL, NULL, NULL, NULL},
      {HAP_CHARACTER_MODEL, (void *)MODEL_NAME, NULL, NULL, NULL, NULL},
      {HAP_CHARACTER_NAME, (void *)ACCESSORY_NAME, NULL, NULL, NULL, NULL},
      {HAP_CHARACTER_SERIAL_NUMBER, (void *)"0123456789", NULL, NULL, NULL, NULL},
      {HAP_CHARACTER_FIRMWARE_REVISION, (void *)"1.0", NULL, NULL, NULL, NULL},
  };
  hap_service_and_characteristics_add(a, accessory_object, HAP_SERVICE_ACCESSORY_INFORMATION, cs, ARRAY_SIZE(cs));

  struct hap_characteristic cc[] = {
      {HAP_CHARACTER_ON, (void *)led, NULL, led_read, led_write, led_notify},
      {HAP_CHARACTER_BRIGHTNESS, (void *)bright, NULL, bri_read, bri_write, bri_notify},
      {HAP_CHARACTER_HUE, (void *)hue, NULL, hue_read, hue_write, hue_notify},
      {HAP_CHARACTER_SATURATION, (void *)saturation, NULL, sat_read, sat_write, sat_notify},
      {HAP_CHARACTER_NAME, (void *)"LightBulb", NULL, NULL, NULL, NULL},
  };
  hap_service_and_characteristics_add(a, accessory_object, HAP_SERVICE_LIGHTBULB, cc, ARRAY_SIZE(cc));
  // hap_service_and_characteristics_add(a, accessory_object, HAP_SERVICE_SWITCHS, cc, ARRAY_SIZE(cc));
}

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
  switch (event->event_id)
  {
  case SYSTEM_EVENT_STA_START:
    esp_wifi_connect();
    break;
  case SYSTEM_EVENT_STA_GOT_IP:
    ESP_LOGI(TAG, "got ip:%s", ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
    xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    {
      hap_init();

      uint8_t mac[6];
      esp_wifi_get_mac(ESP_IF_WIFI_STA, mac);
      char accessory_id[32] = {
          0,
      };
      sprintf(accessory_id, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      hap_accessory_callback_t callback;
      callback.hap_object_init = hap_object_init;
      a = hap_accessory_register((char *)ACCESSORY_NAME, accessory_id, (char *)ACCESSORY_CODE, (char *)MANUFACTURER_NAME, HAP_ACCESSORY_CATEGORY_LIGHTBULB, 811, 1, NULL, &callback);
    }
    break;
  case SYSTEM_EVENT_STA_DISCONNECTED:
    esp_wifi_connect();
    xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
    break;
  default:
    break;
  }
  return ESP_OK;
}

void wifi_init_sta()
{
  wifi_event_group = xEventGroupCreate();

  tcpip_adapter_init();
  ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  wifi_config_t wifi_config = {
      .sta = {
          {.ssid = EXAMPLE_ESP_WIFI_SSID},
          {.password = EXAMPLE_ESP_WIFI_PASS}},
  };

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "wifi_init_sta finished.");
  ESP_LOGI(TAG, "connect to ap SSID:%s password:%s",
           EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
}

void setup()
{
  M5.begin();

  esp_err_t init = nvs_flash_init();
  while (init != ESP_OK)
  {
    init = nvs_flash_erase();
    ESP_LOGI(TAG, "nvs_flash_erase:%d", init);
    init = nvs_flash_init();
    ESP_LOGI(TAG, "nvs_flash_init:%d", init);
  }

  wifi_init_sta();

  M5.Lcd.setTextSize(2);
  M5.Lcd.clear();
}

void loop()
{
  if (M5.BtnA.wasPressed())
  {
    M5.Lcd.setBrightness(50);
    int16_t h = M5.Lcd.fontHeight(2);
    M5.Lcd.drawCentreString(ACCESSORY_CODE, 160, 120 - h / 2, 2);
    // M5.Lcd.drawCentreString("0535", 160, 0, 2);
    // M5.Lcd.drawCentreString("8201", 160, h, 2);
  }

  M5.update();
}