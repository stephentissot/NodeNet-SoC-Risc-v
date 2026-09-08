#include "wifi_manager.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdio>
#include <cstring>

#include <esp_check.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <lwip/ip4_addr.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "app_config.h"

namespace wifi_manager {
namespace {

constexpr const char* kLogTag = "wifi-manager";
constexpr const char* kNamespace = "wifi";
constexpr const char* kKeySsid = "ssid";
constexpr const char* kKeyPassword = "password";
constexpr const char* kKeyLanguage = "language";
constexpr const char* kKeyAnonymous = "anonymous";
constexpr const char* kDefaultLanguage = "uk";
constexpr const char* kSupportedLanguages[] = {"uk", "fr", "de", "es", "zh-Hans"};

SemaphoreHandle_t g_mutex = nullptr;
bool g_initialized = false;
bool g_ap_started = false;
bool g_sta_has_credentials = false;
bool g_sta_connected = false;
bool g_anonymous_access = true;
char g_ap_ssid[33] = {};
char g_sta_ssid[33] = {};
char g_ap_ip[16] = "192.168.4.1";
char g_sta_ip[16] = {};
char g_language[16] = "uk";
esp_netif_t* g_ap_netif = nullptr;
esp_netif_t* g_sta_netif = nullptr;

void copy_text(char* dst, size_t dst_size, const char* src)
{
    if ((dst == nullptr) || (dst_size == 0u)) {
        return;
    }

    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }

    std::snprintf(dst, dst_size, "%s", src);
}

void lock()
{
    if (g_mutex != nullptr) {
        xSemaphoreTake(g_mutex, portMAX_DELAY);
    }
}

void unlock()
{
    if (g_mutex != nullptr) {
        xSemaphoreGive(g_mutex);
    }
}

esp_err_t ensure_nvs_ready()
{
    esp_err_t err = nvs_flash_init();
    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

bool load_station_credentials_from_nvs(char* ssid, size_t ssid_size, char* password, size_t password_size)
{
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    size_t stored_ssid_size = ssid_size;
    const esp_err_t ssid_err = nvs_get_str(handle, kKeySsid, ssid, &stored_ssid_size);
    if (ssid_err != ESP_OK) {
        nvs_close(handle);
        return false;
    }

    size_t stored_password_size = password_size;
    const esp_err_t password_err = nvs_get_str(handle, kKeyPassword, password, &stored_password_size);
    nvs_close(handle);
    if (password_err != ESP_OK) {
        password[0] = '\0';
    }
    return true;
}

esp_err_t save_station_credentials_to_nvs(const char* ssid, const char* password)
{
    nvs_handle_t handle = 0;
    ESP_RETURN_ON_ERROR(nvs_open(kNamespace, NVS_READWRITE, &handle), kLogTag, "nvs_open failed");
    esp_err_t result = nvs_set_str(handle, kKeySsid, ssid);
    if (result == ESP_OK) {
        result = nvs_set_str(handle, kKeyPassword, (password != nullptr) ? password : "");
    }
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

bool is_supported_language(const char* language)
{
    if ((language == nullptr) || (language[0] == '\0')) {
        return false;
    }

    for (const char* supported : kSupportedLanguages) {
        if (std::strcmp(language, supported) == 0) {
            return true;
        }
    }

    return false;
}

void apply_runtime_settings(const char* language, bool anonymous_access)
{
    lock();
    copy_text(g_language, sizeof(g_language), is_supported_language(language) ? language : kDefaultLanguage);
    g_anonymous_access = anonymous_access;
    unlock();
}

void load_site_settings_from_nvs()
{
    char language[sizeof(g_language)] = {};
    copy_text(language, sizeof(language), kDefaultLanguage);
    bool anonymous_access = true;

    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) == ESP_OK) {
        size_t language_size = sizeof(language);
        const esp_err_t language_err = nvs_get_str(handle, kKeyLanguage, language, &language_size);
        if ((language_err != ESP_OK) || !is_supported_language(language)) {
            copy_text(language, sizeof(language), kDefaultLanguage);
        }

        uint8_t anonymous_value = 1u;
        if (nvs_get_u8(handle, kKeyAnonymous, &anonymous_value) == ESP_OK) {
            anonymous_access = (anonymous_value != 0u);
        }

        nvs_close(handle);
    }

    apply_runtime_settings(language, anonymous_access);
}

esp_err_t save_site_settings_to_nvs(const char* language, bool anonymous_access)
{
    nvs_handle_t handle = 0;
    ESP_RETURN_ON_ERROR(nvs_open(kNamespace, NVS_READWRITE, &handle), kLogTag, "nvs_open failed");

    esp_err_t result = nvs_set_str(handle, kKeyLanguage, language);
    if (result == ESP_OK) {
        result = nvs_set_u8(handle, kKeyAnonymous, anonymous_access ? 1u : 0u);
    }
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }

    nvs_close(handle);
    return result;
}

void update_sta_ip(const esp_ip4_addr_t& address)
{
    lock();
    std::snprintf(g_sta_ip,
                  sizeof(g_sta_ip),
                  IPSTR,
                  IP2STR(&address));
    unlock();
}

void wifi_event_handler(void*, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    (void)event_data;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_AP_START:
            lock();
            g_ap_started = true;
            unlock();
            ESP_LOGI(kLogTag, "Setup AP started ssid=%s ip=%s", g_ap_ssid, g_ap_ip);
            break;
        case WIFI_EVENT_STA_START:
            if (g_sta_has_credentials) {
                esp_wifi_connect();
            }
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            lock();
            g_sta_connected = false;
            g_sta_ip[0] = '\0';
            unlock();
            if (g_sta_has_credentials) {
                esp_wifi_connect();
            }
            break;
        default:
            break;
        }
    }

    if ((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP)) {
        const auto* got_ip = static_cast<ip_event_got_ip_t*>(event_data);
        lock();
        g_sta_connected = true;
        unlock();
        update_sta_ip(got_ip->ip_info.ip);
        ESP_LOGI(kLogTag, "STA connected ip=" IPSTR, IP2STR(&got_ip->ip_info.ip));
    }
}

void apply_runtime_status(const char* sta_ssid, bool has_credentials)
{
    lock();
    g_sta_has_credentials = has_credentials;
    copy_text(g_ap_ssid, sizeof(g_ap_ssid), app_config::kSetupApSsid);
    copy_text(g_sta_ssid, sizeof(g_sta_ssid), has_credentials ? sta_ssid : "");
    if (!has_credentials) {
        g_sta_connected = false;
        g_sta_ip[0] = '\0';
    }
    unlock();
}

esp_err_t apply_station_config(const char* ssid, const char* password)
{
    wifi_config_t sta_config = {};
    copy_text(reinterpret_cast<char*>(sta_config.sta.ssid), sizeof(sta_config.sta.ssid), ssid);
    copy_text(reinterpret_cast<char*>(sta_config.sta.password), sizeof(sta_config.sta.password), password);
    sta_config.sta.threshold.authmode = (password != nullptr && password[0] != '\0')
                                      ? WIFI_AUTH_WPA2_PSK
                                      : WIFI_AUTH_OPEN;
    sta_config.sta.pmf_cfg.capable = true;
    sta_config.sta.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta_config), kLogTag, "esp_wifi_set_config(sta) failed");
    ESP_RETURN_ON_ERROR(esp_wifi_disconnect(), kLogTag, "esp_wifi_disconnect failed");
    return esp_wifi_connect();
}

} // namespace

esp_err_t init()
{
    if (g_initialized) {
        return ESP_OK;
    }

    if (g_mutex == nullptr) {
        g_mutex = xSemaphoreCreateMutex();
        if (g_mutex == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_RETURN_ON_ERROR(ensure_nvs_ready(), kLogTag, "nvs init failed");
    load_site_settings_from_nvs();

    esp_err_t err = esp_netif_init();
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        return err;
    }

    err = esp_event_loop_create_default();
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        return err;
    }

    g_ap_netif = esp_netif_create_default_wifi_ap();
    g_sta_netif = esp_netif_create_default_wifi_sta();
    if ((g_ap_netif == nullptr) || (g_sta_netif == nullptr)) {
        return ESP_FAIL;
    }

    esp_netif_set_hostname(g_sta_netif, app_config::kSetupHostname);

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), kLogTag, "esp_wifi_init failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr),
                        kLogTag,
                        "wifi event register failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr),
                        kLogTag,
                        "ip event register failed");

    wifi_config_t ap_config = {};
    copy_text(reinterpret_cast<char*>(ap_config.ap.ssid), sizeof(ap_config.ap.ssid), app_config::kSetupApSsid);
    copy_text(reinterpret_cast<char*>(ap_config.ap.password), sizeof(ap_config.ap.password), app_config::kSetupApPassword);
    ap_config.ap.ssid_len = std::strlen(app_config::kSetupApSsid);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_config.ap.pmf_cfg.required = false;

    char sta_ssid[33] = {};
    char sta_password[65] = {};
    const bool have_credentials = load_station_credentials_from_nvs(sta_ssid, sizeof(sta_ssid), sta_password, sizeof(sta_password));
    apply_runtime_status(sta_ssid, have_credentials);

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), kLogTag, "esp_wifi_set_mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), kLogTag, "esp_wifi_set_config(ap) failed");
    if (have_credentials) {
        ESP_RETURN_ON_ERROR(apply_station_config(sta_ssid, sta_password), kLogTag, "apply stored station config failed");
    }
    ESP_RETURN_ON_ERROR(esp_wifi_start(), kLogTag, "esp_wifi_start failed");
    if (have_credentials) {
        ESP_RETURN_ON_ERROR(apply_station_config(sta_ssid, sta_password), kLogTag, "station connect failed");
    }

    g_initialized = true;
    return ESP_OK;
}

esp_err_t preload_site_settings()
{
    if (g_mutex == nullptr) {
        g_mutex = xSemaphoreCreateMutex();
        if (g_mutex == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_RETURN_ON_ERROR(ensure_nvs_ready(), kLogTag, "nvs init failed");
    load_site_settings_from_nvs();
    return ESP_OK;
}

bool has_saved_credentials()
{
    lock();
    const bool value = g_sta_has_credentials;
    unlock();
    return value;
}

esp_err_t set_station_credentials(const char* ssid, const char* password)
{
    if ((ssid == nullptr) || (ssid[0] == '\0')) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(save_station_credentials_to_nvs(ssid, password), kLogTag, "nvs save failed");
    apply_runtime_status(ssid, true);

    if (!g_initialized) {
        return ESP_OK;
    }

    return apply_station_config(ssid, (password != nullptr) ? password : "");
}

bool is_anonymous_access_allowed()
{
    lock();
    const bool value = g_anonymous_access;
    unlock();
    return value;
}

SiteSettings get_site_settings()
{
    SiteSettings settings = {};
    lock();
    copy_text(settings.language, sizeof(settings.language), g_language);
    settings.anonymous_access = g_anonymous_access;
    unlock();
    return settings;
}

esp_err_t set_site_settings(const char* language, bool anonymous_access)
{
    if (!is_supported_language(language)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(save_site_settings_to_nvs(language, anonymous_access), kLogTag, "settings save failed");
    apply_runtime_settings(language, anonymous_access);
    return ESP_OK;
}

Status get_status()
{
    Status status = {};
    lock();
    status.ap_started = g_ap_started;
    status.sta_has_credentials = g_sta_has_credentials;
    status.sta_connected = g_sta_connected;
    copy_text(status.ap_ssid, sizeof(status.ap_ssid), g_ap_ssid);
    copy_text(status.sta_ssid, sizeof(status.sta_ssid), g_sta_ssid);
    copy_text(status.ap_ip, sizeof(status.ap_ip), g_ap_ip);
    copy_text(status.sta_ip, sizeof(status.sta_ip), g_sta_ip);
    unlock();
    return status;
}

} // namespace wifi_manager
