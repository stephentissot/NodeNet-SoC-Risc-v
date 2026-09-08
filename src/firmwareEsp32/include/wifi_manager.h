#pragma once

#include <esp_err.h>

namespace wifi_manager {

struct Status {
    bool ap_started;
    bool sta_has_credentials;
    bool sta_connected;
    char ap_ssid[33];
    char sta_ssid[33];
    char ap_ip[16];
    char sta_ip[16];
};

struct SiteSettings {
    char language[16];
    bool anonymous_access;
};

esp_err_t init();
esp_err_t preload_site_settings();
bool has_saved_credentials();
esp_err_t set_station_credentials(const char* ssid, const char* password);
bool is_anonymous_access_allowed();
SiteSettings get_site_settings();
esp_err_t set_site_settings(const char* language, bool anonymous_access);
Status get_status();

} // namespace wifi_manager
