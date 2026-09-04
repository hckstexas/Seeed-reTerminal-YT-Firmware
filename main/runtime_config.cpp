#include "runtime_config.h"

#include <cstring>
#include <memory>
#include <new>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
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
constexpr size_t kRequestBodyCapacity = 2048;

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

void form_value(const char *body, const char *key, char *destination, size_t size) {
    if (size == 0) return;
    destination[0] = '\0';
    const size_t key_length = std::strlen(key);
    const char *cursor = body;
    while (cursor != nullptr && *cursor != '\0') {
        const char *equals = std::strchr(cursor, '=');
        const char *ampersand = std::strchr(cursor, '&');
        if (equals != nullptr && (ampersand == nullptr || equals < ampersand) &&
            static_cast<size_t>(equals - cursor) == key_length &&
            std::strncmp(cursor, key, key_length) == 0) {
            const char *value_start = equals + 1;
            const size_t value_length = ampersand == nullptr
                ? std::strlen(value_start)
                : static_cast<size_t>(ampersand - value_start);
            size_t written = 0;
            for (size_t index = 0; index < value_length; ++index) {
                char decoded = value_start[index];
                if (decoded == '+') {
                    decoded = ' ';
                } else if (decoded == '%' && index + 2 < value_length &&
                           hex_value(value_start[index + 1]) >= 0 &&
                           hex_value(value_start[index + 2]) >= 0) {
                    decoded = static_cast<char>(
                        (hex_value(value_start[index + 1]) << 4) |
                        hex_value(value_start[index + 2]));
                    index += 2;
                }
                if (written + 1 < size) {
                    destination[written++] = decoded;
                }
            }
            destination[written] = '\0';
            return;
        }
        cursor = ampersand == nullptr ? nullptr : ampersand + 1;
    }
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
    if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK ||
        esp_wifi_set_config(WIFI_IF_AP, &config) != ESP_OK) return false;
    return esp_wifi_start() == ESP_OK;
}

bool read_request_body(httpd_req_t *request, char *body, size_t capacity) {
    if (request->content_len <= 0 || static_cast<size_t>(request->content_len) >= capacity) return false;
    size_t received_total = 0;
    while (received_total < static_cast<size_t>(request->content_len)) {
        const int received = httpd_req_recv(
            request,
            body + received_total,
            request->content_len - received_total);
        if (received <= 0) return false;
        received_total += static_cast<size_t>(received);
    }
    body[received_total] = '\0';
    return true;
}

esp_err_t send_page(httpd_req_t *request, const char *status, const char *html) {
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, html, HTTPD_RESP_USE_STRLEN);
}

esp_err_t index_handler(httpd_req_t *request) {
    static constexpr char kHtml[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Sticky YouTube setup</title>
<style>body{font-family:system-ui,sans-serif;max-width:34rem;margin:2rem auto;padding:0 1rem;color:#171717}label{display:block;margin:1rem 0 .35rem;font-weight:600}input{box-sizing:border-box;width:100%;padding:.75rem;border:1px solid #999;border-radius:.5rem;font:inherit}button{margin-top:1.25rem;padding:.75rem 1rem;border:0;border-radius:.5rem;background:#171717;color:#fff;font-weight:700}p{line-height:1.5;color:#555}</style></head>
<body><h1>Sticky YouTube setup</h1><p>Enter the Wi-Fi network and YouTube Data API key for this device. The key is stored locally in NVS and is not sent to a server.</p>
<form method="post" action="/save"><label for="ssid">Wi-Fi network</label><input id="ssid" name="ssid" required autocomplete="off"><label for="password">Wi-Fi password</label><input id="password" name="password" type="password" autocomplete="off"><label for="api_key">YouTube Data API key</label><input id="api_key" name="api_key" type="password" required autocomplete="off"><label for="channel">Channel ID or @handle</label><input id="channel" name="channel" value="@PBJSquad" required autocomplete="off"><button type="submit">Save and restart Sticky</button></form></body></html>)HTML";
    return send_page(request, "200 OK", kHtml);
}

esp_err_t save_handler(httpd_req_t *request) {
    std::unique_ptr<char[]> body(new (std::nothrow) char[kRequestBodyCapacity] {});
    if (!body || !read_request_body(request, body.get(), kRequestBodyCapacity)) {
        return send_page(request, "400 Bad Request", "<h1>Invalid setup request</h1><p>The form data was missing or too large.</p>");
    }

    YoutubeConfig config;
    form_value(body.get(), "ssid", config.wifi_ssid, sizeof(config.wifi_ssid));
    form_value(body.get(), "password", config.wifi_password, sizeof(config.wifi_password));
    form_value(body.get(), "api_key", config.api_key, sizeof(config.api_key));
    form_value(body.get(), "channel", config.channel, sizeof(config.channel));
    if (!config.ready()) {
        return send_page(request, "400 Bad Request", "<h1>Missing setup value</h1><p>Wi-Fi, API key, and channel are required.</p>");
    }
    if (!save_youtube_config(config)) {
        return send_page(request, "500 Internal Server Error", "<h1>Could not save setup</h1><p>NVS could not store the configuration.</p>");
    }

    const esp_err_t result = send_page(request, "200 OK", "<h1>Saved</h1><p>The Sticky will restart and fetch YouTube directly.</p>");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
    return result;
}
}

bool YoutubeConfig::ready() const {
    return wifi_ssid[0] != '\0' && api_key[0] != '\0' && channel[0] != '\0';
}

bool load_youtube_config(YoutubeConfig &config) {
    config = {};
    copy_value(config.wifi_ssid, sizeof(config.wifi_ssid), CONFIG_YOUTUBE_WIFI_SSID);
    copy_value(config.wifi_password, sizeof(config.wifi_password), CONFIG_YOUTUBE_WIFI_PASSWORD);
    copy_value(config.api_key, sizeof(config.api_key), CONFIG_YOUTUBE_API_KEY);
    copy_value(config.channel, sizeof(config.channel), CONFIG_YOUTUBE_CHANNEL);

    nvs_handle_t handle;
    if (nvs_open("youtube", NVS_READONLY, &handle) != ESP_OK) return config.ready();
    size_t size = sizeof(config.wifi_ssid); nvs_get_str(handle, "ssid", config.wifi_ssid, &size);
    size = sizeof(config.wifi_password); nvs_get_str(handle, "password", config.wifi_password, &size);
    size = sizeof(config.api_key); nvs_get_str(handle, "api_key", config.api_key, &size);
    size = sizeof(config.channel); nvs_get_str(handle, "channel", config.channel, &size);
    nvs_close(handle);
    return config.ready();
}

bool save_youtube_config(const YoutubeConfig &config) {
    nvs_handle_t handle;
    if (nvs_open("youtube", NVS_READWRITE, &handle) != ESP_OK) return false;
    const bool ok =
        nvs_set_str(handle, "ssid", config.wifi_ssid) == ESP_OK &&
        nvs_set_str(handle, "password", config.wifi_password) == ESP_OK &&
        nvs_set_str(handle, "api_key", config.api_key) == ESP_OK &&
        nvs_set_str(handle, "channel", config.channel) == ESP_OK &&
        nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}

bool start_youtube_provisioning() {
    if (!start_ap()) return false;
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.stack_size = 8192;
    if (httpd_start(&s_server, &server_config) != ESP_OK) return false;
    httpd_uri_t index_uri = {};
    index_uri.uri = "/";
    index_uri.method = HTTP_GET;
    index_uri.handler = index_handler;
    httpd_register_uri_handler(s_server, &index_uri);
    httpd_uri_t save_uri = {};
    save_uri.uri = "/save";
    save_uri.method = HTTP_POST;
    save_uri.handler = save_handler;
    httpd_register_uri_handler(s_server, &save_uri);
    ESP_LOGI(kTag, "Setup AP ready: SSID=%s password=%s address=http://192.168.4.1", kApSsid, kApPassword);
    return true;
}
