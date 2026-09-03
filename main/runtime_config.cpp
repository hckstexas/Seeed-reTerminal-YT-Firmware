#include "runtime_config.h"

#include <cstring>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_restart.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sdkconfig.h"

namespace {
constexpr const char *kTag = "runtime_config";
constexpr const char *kApSsid = "Sticky-YT";
constexpr const char *kApPassword = "stickysetup";
httpd_handle_t s_server = nullptr;

void copy_value(char *destination, size_t size, const char *source) {
    if (size == 0) return;
    std::strncpy(destination, source == nullptr ? "" : source, size - 1);
    destination[size - 1] = '\0';
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void url_decode(char *value) {
    char *read = value;
    char *write = value;
    while (*read != '\0') {
        if (*read == '+') { *write++ = ' '; ++read; }
        else if (*read == '%' && hex_value(read[1]) >= 0 && hex_value(read[2]) >= 0) {
            *write++ = static_cast<char>((hex_value(read[1]) << 4) | hex_value(read[2]));
            read += 3;
        } else *write++ = *read++;
    }
    *write = '\0';
}

void form_value(char *body, const char *key, char *destination, size_t size) {
    destination[0] = '\0';
    const size_t key_length = std::strlen(key);
    for (char *part = std::strtok(body, "&"); part != nullptr; part = std::strtok(nullptr, "&")) {
        if (std::strncmp(part, key, key_length) == 0 && part[key_length] == '=') {
            url_decode(part + key_length + 1);
            copy_value(destination, size, part + key_length + 1);
            return;
        }
    }
}

httpd_handle_t start_http_server();

esp_err_t index_handler(httpd_req_t *request) {
    static const char html[] = "<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'><title>Sticky YouTube setup</title></head><body style='font:16px sans-serif;max-width:520px;margin:2rem auto;padding:0 1rem'><h1>Sticky YouTube setup</h1><p>Enter Wi-Fi and your deployed HTTPS YouTube proxy endpoint.</p><form method=post action=/save><label>Wi-Fi SSID<br><input name=ssid required maxlength=32 style='width:100%'></label><br><label>Wi-Fi password<br><input name=password type=password maxlength=64 style='width:100%'></label><br><label>HTTPS stats endpoint<br><input name=endpoint type=url required placeholder='https://your-domain/api/youtube/stats' style='width:100%'></label><br><label>YouTube channel ID or @handle<br><input name=channel value='@PBJSquad' required maxlength=96 style='width:100%'></label><br><button type=submit>Save and restart Sticky</button></form></body></html>";
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, html, HTTPD_RESP_USE_STRLEN);
}

esp_err_t save_handler(httpd_req_t *request) {
    char body[1024] = {};
    const int expected = request->content_len;
    if (expected <= 0 || expected >= static_cast<int>(sizeof(body))) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Form is too large");
    int received = 0;
    while (received < expected) {
        const int count = httpd_req_recv(request, body + received, expected - received);
        if (count <= 0) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Could not read form");
        received += count;
    }
    body[received] = '\0';
    YoutubeConfig config;
    form_value(body, "ssid", config.wifi_ssid, sizeof(config.wifi_ssid));
    form_value(body, "password", config.wifi_password, sizeof(config.wifi_password));
    form_value(body, "endpoint", config.endpoint, sizeof(config.endpoint));
    form_value(body, "channel", config.channel, sizeof(config.channel));
    if (!config.ready() || !save_youtube_config(config)) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save configuration");
    const char response[] = "Saved. Restarting Sticky now.";
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    httpd_resp_send(request, response, HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

bool start_ap() {
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK && err != ESP_ERR_WIFI_INIT_STATE) return false;
    wifi_config_t config = {};
    copy_value(reinterpret_cast<char *>(config.ap.ssid), sizeof(config.ap.ssid), kApSsid);
    copy_value(reinterpret_cast<char *>(config.ap.password), sizeof(config.ap.password), kApPassword);
    config.ap.ssid_len = std::strlen(kApSsid);
    config.ap.channel = 1;
    config.ap.max_connection = 2;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK || esp_wifi_set_config(WIFI_IF_AP, &config) != ESP_OK) return false;
    return esp_wifi_start() == ESP_OK;
}
}

bool YoutubeConfig::ready() const { return wifi_ssid[0] != '\0' && endpoint[0] != '\0' && channel[0] != '\0'; }

bool load_youtube_config(YoutubeConfig &config) {
    config = {};
    copy_value(config.wifi_ssid, sizeof(config.wifi_ssid), CONFIG_YOUTUBE_WIFI_SSID);
    copy_value(config.wifi_password, sizeof(config.wifi_password), CONFIG_YOUTUBE_WIFI_PASSWORD);
    copy_value(config.endpoint, sizeof(config.endpoint), CONFIG_YOUTUBE_API_BASE_URL);
    copy_value(config.channel, sizeof(config.channel), CONFIG_YOUTUBE_CHANNEL);
    nvs_handle_t handle;
    if (nvs_open("youtube", NVS_READONLY, &handle) != ESP_OK) return config.ready();
    size_t size = sizeof(config.wifi_ssid); nvs_get_str(handle, "ssid", config.wifi_ssid, &size);
    size = sizeof(config.wifi_password); nvs_get_str(handle, "password", config.wifi_password, &size);
    size = sizeof(config.endpoint); nvs_get_str(handle, "endpoint", config.endpoint, &size);
    size = sizeof(config.channel); nvs_get_str(handle, "channel", config.channel, &size);
    nvs_close(handle);
    return config.ready();
}

bool save_youtube_config(const YoutubeConfig &config) {
    nvs_handle_t handle;
    if (nvs_open("youtube", NVS_READWRITE, &handle) != ESP_OK) return false;
    const bool ok = nvs_set_str(handle, "ssid", config.wifi_ssid) == ESP_OK && nvs_set_str(handle, "password", config.wifi_password) == ESP_OK && nvs_set_str(handle, "endpoint", config.endpoint) == ESP_OK && nvs_set_str(handle, "channel", config.channel) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}

bool start_youtube_provisioning() {
    if (!start_ap()) return false;
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&s_server, &server_config) != ESP_OK) return false;
    httpd_uri_t index_uri = {}; index_uri.uri = "/"; index_uri.method = HTTP_GET; index_uri.handler = index_handler; httpd_register_uri_handler(s_server, &index_uri);
    httpd_uri_t save_uri = {}; save_uri.uri = "/save"; save_uri.method = HTTP_POST; save_uri.handler = save_handler; httpd_register_uri_handler(s_server, &save_uri);
    ESP_LOGI(kTag, "Setup AP ready: SSID=%s password=%s address=http://192.168.4.1", kApSsid, kApPassword);
    return true;
}
