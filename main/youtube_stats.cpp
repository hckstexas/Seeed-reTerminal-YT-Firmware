#include "youtube_stats.h"
#include "runtime_config.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace {
constexpr const char *kTag = "youtube_stats";
constexpr EventBits_t kWifiConnected = BIT0;
EventGroupHandle_t s_wifi_events = nullptr;
bool s_wifi_started = false;
bool s_wifi_ready = false;

void set_error(char *buffer, uint32_t size, const char *message) {
    if (buffer != nullptr && size > 0) std::strncpy(buffer, message, size - 1);
    if (buffer != nullptr && size > 0) buffer[size - 1] = '\0';
}

void wifi_event_handler(void *, esp_event_base_t event_base, int32_t event_id, void *) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_ready = false;
        if (s_wifi_events != nullptr) xEventGroupClearBits(s_wifi_events, kWifiConnected);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_ready = true;
        if (s_wifi_events != nullptr) xEventGroupSetBits(s_wifi_events, kWifiConnected);
    }
}

bool ensure_wifi(const YoutubeConfig &config, char *error_message, uint32_t error_message_size) {
    if (s_wifi_ready) return true;
    if (config.wifi_ssid[0] == '\0') {
        set_error(error_message, error_message_size, "Wi-Fi SSID is not configured");
        return false;
    }
    if (!s_wifi_started) {
        if (s_wifi_events == nullptr) s_wifi_events = xEventGroupCreate();
        if (s_wifi_events == nullptr) {
            set_error(error_message, error_message_size, "Wi-Fi event group failed");
            return false;
        }
        esp_err_t err = esp_netif_init();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            set_error(error_message, error_message_size, "Network stack init failed");
            return false;
        }
        err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            set_error(error_message, error_message_size, "Event loop init failed");
            return false;
        }
        esp_netif_create_default_wifi_sta();
        wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
        err = esp_wifi_init(&init_config);
        if (err != ESP_OK && err != ESP_ERR_WIFI_INIT_STATE) {
            set_error(error_message, error_message_size, "Wi-Fi init failed");
            return false;
        }
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr);
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr);
        wifi_config_t wifi_config = {};
        std::strncpy(reinterpret_cast<char *>(wifi_config.sta.ssid), config.wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
        std::strncpy(reinterpret_cast<char *>(wifi_config.sta.password), config.wifi_password, sizeof(wifi_config.sta.password) - 1);
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        err = esp_wifi_start();
        if (err != ESP_OK && err != ESP_ERR_WIFI_STATE) {
            set_error(error_message, error_message_size, "Wi-Fi start failed");
            return false;
        }
        s_wifi_started = true;
    }
    const EventBits_t bits = xEventGroupWaitBits(s_wifi_events, kWifiConnected, pdFALSE, pdFALSE, pdMS_TO_TICKS(20000));
    if ((bits & kWifiConnected) == 0) {
        set_error(error_message, error_message_size, "Wi-Fi connection timed out");
        return false;
    }
    return true;
}

std::string url_encode(const char *value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    for (const unsigned char *cursor = reinterpret_cast<const unsigned char *>(value); *cursor != '\0'; ++cursor) {
        const unsigned char character = *cursor;
        if (std::isalnum(character) || character == '-' || character == '_' || character == '.' || character == '~') {
            encoded.push_back(static_cast<char>(character));
        } else {
            encoded.push_back('%');
            encoded.push_back(kHex[(character >> 4) & 0x0F]);
            encoded.push_back(kHex[character & 0x0F]);
        }
    }
    return encoded;
}

bool count_field(cJSON *object, const char *name, uint64_t &value) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (item == nullptr) return false;
    if (cJSON_IsNumber(item) && item->valuedouble >= 0) {
        value = static_cast<uint64_t>(item->valuedouble);
        return true;
    }
    if (!cJSON_IsString(item) || item->valuestring == nullptr || item->valuestring[0] == '\0') return false;
    char *end = nullptr;
    const unsigned long long parsed = std::strtoull(item->valuestring, &end, 10);
    if (end == item->valuestring || *end != '\0') return false;
    value = static_cast<uint64_t>(parsed);
    return true;
}
}

bool fetch_youtube_stats(const YoutubeConfig &config, YoutubeStats &stats, char *error_message, uint32_t error_message_size) {
    stats = {};
    if (!ensure_wifi(config, error_message, error_message_size)) return false;
    if (config.api_key[0] == '\0') {
        set_error(error_message, error_message_size, "YouTube API key is not configured");
        return false;
    }
    if (config.channel[0] == '\0') {
        set_error(error_message, error_message_size, "YouTube channel is not configured");
        return false;
    }

    const bool is_handle = config.channel[0] == '@';
    const char *channel_value = is_handle ? config.channel + 1 : config.channel;
    std::string url = "https://www.googleapis.com/youtube/v3/channels?part=snippet%2Cstatistics&key=";
    url += url_encode(config.api_key);
    url += is_handle ? "&forHandle=" : "&id=";
    url += url_encode(channel_value);

    esp_http_client_config_t http_config = {};
    http_config.url = url.c_str();
    http_config.timeout_ms = 15000;
    http_config.keep_alive_enable = false;
    http_config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == nullptr) {
        set_error(error_message, error_message_size, "HTTPS client init failed");
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        set_error(error_message, error_message_size, "Could not reach YouTube API");
        esp_http_client_cleanup(client);
        return false;
    }
    const int content_length = esp_http_client_fetch_headers(client);
    const int status_code = esp_http_client_get_status_code(client);
    char body[6144] = {};
    int total = 0;
    while (total < static_cast<int>(sizeof(body) - 1)) {
        const int read = esp_http_client_read(client, body + total, sizeof(body) - 1 - total);
        if (read <= 0) break;
        total += read;
    }
    body[total] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (status_code < 200 || status_code >= 300) {
        set_error(error_message, error_message_size, status_code == 403 ? "YouTube API rejected the key" : "YouTube API returned an error");
        return false;
    }
    if (total == 0 || (content_length > 0 && content_length >= static_cast<int>(sizeof(body)))) {
        set_error(error_message, error_message_size, "YouTube response was empty or too large");
        return false;
    }

    cJSON *root = cJSON_Parse(body);
    if (root == nullptr) {
        set_error(error_message, error_message_size, "YouTube response was not valid JSON");
        return false;
    }
    cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
    cJSON *item = cJSON_IsArray(items) ? cJSON_GetArrayItem(items, 0) : nullptr;
    cJSON *statistics = item == nullptr ? nullptr : cJSON_GetObjectItemCaseSensitive(item, "statistics");
    cJSON *snippet = item == nullptr ? nullptr : cJSON_GetObjectItemCaseSensitive(item, "snippet");
    const bool parsed = statistics != nullptr &&
        count_field(statistics, "subscriberCount", stats.subscribers) &&
        count_field(statistics, "viewCount", stats.views) &&
        count_field(statistics, "videoCount", stats.videos);
    cJSON *title = snippet == nullptr ? nullptr : cJSON_GetObjectItemCaseSensitive(snippet, "title");
    if (title != nullptr && cJSON_IsString(title) && title->valuestring != nullptr) {
        std::strncpy(stats.channel_title, title->valuestring, sizeof(stats.channel_title) - 1);
    }
    cJSON_Delete(root);
    if (!parsed) {
        set_error(error_message, error_message_size, "YouTube response missed channel totals");
        return false;
    }
    stats.valid = true;
    ESP_LOGI(kTag, "Fetched channel stats: subscribers=%llu views=%llu videos=%llu", static_cast<unsigned long long>(stats.subscribers), static_cast<unsigned long long>(stats.views), static_cast<unsigned long long>(stats.videos));
    return true;
}
