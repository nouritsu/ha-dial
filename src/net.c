#include "net.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "board.h"
#include "config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "nvs.h"

static const char *TAG = "net";

static esp_netif_t *netif;
static volatile bool got_ip, synced;
static bool running, was_up, fell_back, sntp_started;
static uint32_t started_at;
static uint8_t cached_bssid[6], cached_channel;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    got_ip = false;
    if (running)
      esp_wifi_connect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    got_ip = true;
  }
}

static void on_sync(struct timeval *tv) { synced = true; }

static void configure(bool use_cache) {
  wifi_config_t config = {.sta = {.scan_method = WIFI_FAST_SCAN}};
  strlcpy((char *)config.sta.ssid, WIFI_SSID, sizeof config.sta.ssid);
  strlcpy((char *)config.sta.password, WIFI_PASSWORD,
          sizeof config.sta.password);
  if (use_cache && cached_channel) {
    config.sta.bssid_set = true;
    memcpy(config.sta.bssid, cached_bssid, sizeof cached_bssid);
    config.sta.channel = cached_channel;
  }
  esp_wifi_set_config(WIFI_IF_STA, &config);
}

static void load_cache(void) {
  nvs_handle_t nvs;
  size_t size = sizeof cached_bssid;
  if (nvs_open("wifi", NVS_READONLY, &nvs) != ESP_OK)
    return;
  if (nvs_get_u8(nvs, "ch", &cached_channel) != ESP_OK ||
      nvs_get_blob(nvs, "bssid", cached_bssid, &size) != ESP_OK)
    cached_channel = 0;
  nvs_close(nvs);
}

static void save_cache(void) {
  wifi_ap_record_t ap;
  nvs_handle_t nvs;
  if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK)
    return;
  if (ap.primary == cached_channel &&
      !memcmp(ap.bssid, cached_bssid, sizeof cached_bssid))
    return;
  memcpy(cached_bssid, ap.bssid, sizeof cached_bssid);
  cached_channel = ap.primary;
  if (nvs_open("wifi", NVS_READWRITE, &nvs) != ESP_OK)
    return;
  nvs_set_u8(nvs, "ch", cached_channel);
  nvs_set_blob(nvs, "bssid", cached_bssid, sizeof cached_bssid);
  nvs_commit(nvs);
  nvs_close(nvs);
}

static void init_static_ip(void) {
#ifdef STATIC_IP
  esp_netif_ip_info_t ip = {0};
  esp_netif_dns_info_t dns = {0};
  esp_netif_str_to_ip4(STATIC_IP, &ip.ip);
  esp_netif_str_to_ip4(GATEWAY, &ip.gw);
  esp_netif_str_to_ip4(SUBNET, &ip.netmask);
  esp_netif_dhcpc_stop(netif);
  esp_netif_set_ip_info(netif, &ip);
#ifdef DNS
  esp_netif_str_to_ip4(DNS, &dns.ip.u_addr.ip4);
#else
  dns.ip.u_addr.ip4 = ip.gw;
#endif
  dns.ip.type = ESP_IPADDR_TYPE_V4;
  esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
#endif
}

void net_init(void) {
  setenv("TZ", DIAL_TIMEZONE, 1);
  tzset();
  time_t utc;
  if (board_rtc_read(&utc) && utc >= 1735689600)
    settimeofday(&(struct timeval){.tv_sec = utc}, NULL);

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  netif = esp_netif_create_default_wifi_sta();
  wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&config));
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
  init_static_ip();
  load_cache();
}

void net_start(void) {
  if (running)
    return;
  running = true;
  fell_back = false;
  started_at = now_ms();
  configure(true);
  esp_wifi_start();
  esp_wifi_set_ps(WIFI_PS_NONE);
}

void net_stop(void) {
  if (!running)
    return;
  running = false;
  got_ip = was_up = false;
  esp_wifi_disconnect();
  esp_wifi_stop();
}

bool net_up(void) { return got_ip; }

void net_loop(void) {
  if (!running)
    return;
  if (got_ip && !was_up) {
    was_up = true;
    ESP_LOGI(TAG, "up in %lu ms", (unsigned long)(now_ms() - started_at));
    save_cache();
    if (!sntp_started) {
      sntp_started = true;
      esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(DIAL_NTP_SERVER);
      config.sync_cb = on_sync;
      esp_netif_sntp_init(&config);
    }
  } else if (!got_ip) {
    was_up = false;
    if (!fell_back && cached_channel &&
        now_ms() - started_at > WIFI_FALLBACK_MS) {
      fell_back = true;
      ESP_LOGW(TAG, "cached access point unreachable, scanning");
      esp_wifi_disconnect();
      configure(false);
      esp_wifi_connect();
    }
  }
  if (synced) {
    synced = false;
    board_rtc_write(time(NULL));
  }
}
