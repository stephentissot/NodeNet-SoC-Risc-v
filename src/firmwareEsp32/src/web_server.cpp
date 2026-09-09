#include "web_server.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <esp_check.h>
#include <esp_http_server.h>
#include <esp_littlefs.h>
#include <esp_log.h>
#include <sys/stat.h>

#include "app_config.h"
#include "spi_link.h"
#include "wifi_manager.h"

namespace web_server {
namespace {

constexpr const char* kLogTag = "web-server";
constexpr size_t kMaxWsClients = 4;
constexpr uint8_t kPointValueTypeString = 7u;

httpd_handle_t g_server = nullptr;
int g_ws_clients[kMaxWsClients] = {-1, -1, -1, -1};
spi_link::BootProgress g_last_boot = {};
spi_link::SnapshotInfo g_last_snapshot = {};
wifi_manager::Status g_last_wifi = {};
wifi_manager::SiteSettings g_last_settings = {};
bool g_have_last_boot = false;
bool g_have_last_snapshot = false;
bool g_have_last_wifi = false;
bool g_have_last_settings = false;
bool g_fs_mounted = false;

constexpr const char* kSupportedLanguagesJson = "[\"uk\",\"fr\",\"de\",\"es\",\"zh-Hans\"]";

esp_err_t send_error(httpd_req_t* req, const char* status, const char* message);

esp_err_t ensure_fs_mounted()
{
    if (g_fs_mounted) {
        return ESP_OK;
    }

    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = app_config::kWebMountPath;
    conf.partition_label = "www";
    conf.partition = nullptr;
    conf.format_if_mount_failed = false;
    conf.dont_mount = false;
    conf.grow_on_mount = false;

    ESP_RETURN_ON_ERROR(esp_vfs_littlefs_register(&conf), kLogTag, "LittleFS mount failed");
    g_fs_mounted = true;
    return ESP_OK;
}

std::string json_escape(const char* text)
{
    if (text == nullptr) {
        return "";
    }

    std::string out;
    while (*text != '\0') {
        const char ch = *text++;
        switch (ch) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += ch;
            break;
        }
    }
    return out;
}

std::string build_point_path(const char* device_id, const char* feature, const char* point_id)
{
    if ((device_id == nullptr) || (feature == nullptr) || (point_id == nullptr) ||
        (device_id[0] == '\0') || (feature[0] == '\0') || (point_id[0] == '\0')) {
        return "";
    }

    std::string path;
    path.reserve(std::strlen(device_id) + std::strlen(feature) + std::strlen(point_id) + 2u);
    path += device_id;
    path += '.';
    path += feature;
    path += '.';
    path += point_id;
    return path;
}

bool boot_equal(const spi_link::BootProgress& lhs, const spi_link::BootProgress& rhs)
{
    return std::memcmp(&lhs, &rhs, sizeof(lhs)) == 0;
}

bool snapshot_equal(const spi_link::SnapshotInfo& lhs, const spi_link::SnapshotInfo& rhs)
{
    return std::memcmp(&lhs, &rhs, sizeof(lhs)) == 0;
}

bool wifi_equal(const wifi_manager::Status& lhs, const wifi_manager::Status& rhs)
{
    return std::memcmp(&lhs, &rhs, sizeof(lhs)) == 0;
}

bool settings_equal(const wifi_manager::SiteSettings& lhs, const wifi_manager::SiteSettings& rhs)
{
    return std::memcmp(&lhs, &rhs, sizeof(lhs)) == 0;
}

std::string make_boot_json(const spi_link::BootProgress& progress)
{
    char buffer[256] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "{\"type\":\"boot_progress\",\"boot\":{\"percent\":%u,\"loaded_points\":%u,\"total_points\":%u,\"link_ready\":%s,\"caps_received\":%s,\"snapshot_started\":%s,\"snapshot_complete\":%s}}",
                  static_cast<unsigned>(progress.percent),
                  static_cast<unsigned>(progress.loaded_points),
                  static_cast<unsigned>(progress.total_points),
                  progress.link_ready ? "true" : "false",
                  progress.caps_received ? "true" : "false",
                  progress.snapshot_started ? "true" : "false",
                  progress.snapshot_complete ? "true" : "false");
    return buffer;
}

std::string make_snapshot_json(const spi_link::SnapshotInfo& info)
{
    char buffer[256] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "{\"type\":\"plc_snapshot_available\",\"snapshot\":{\"sequence\":%lu,\"point_count\":%u,\"loaded_points\":%u,\"max_payload\":%u,\"complete\":%s}}",
                  static_cast<unsigned long>(info.sequence),
                  static_cast<unsigned>(info.point_count),
                  static_cast<unsigned>(info.loaded_points),
                  static_cast<unsigned>(info.max_payload),
                  info.complete ? "true" : "false");
    return buffer;
}

std::string make_wifi_json_message(const wifi_manager::Status& status)
{
    char buffer[384] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "{\"type\":\"wifi_status\",\"wifi\":{\"ap_started\":%s,\"sta_has_credentials\":%s,\"sta_connected\":%s,\"ap_ssid\":\"%s\",\"sta_ssid\":\"%s\",\"ap_ip\":\"%s\",\"sta_ip\":\"%s\"}}",
                  status.ap_started ? "true" : "false",
                  status.sta_has_credentials ? "true" : "false",
                  status.sta_connected ? "true" : "false",
                  json_escape(status.ap_ssid).c_str(),
                  json_escape(status.sta_ssid).c_str(),
                  json_escape(status.ap_ip).c_str(),
                  json_escape(status.sta_ip).c_str());
    return buffer;
}

std::string make_settings_json_message(const wifi_manager::SiteSettings& settings)
{
    char buffer[256] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "{\"type\":\"settings\",\"settings\":{\"language\":\"%s\",\"anonymous\":%s,\"languages\":%s}}",
                  json_escape(settings.language).c_str(),
                  settings.anonymous_access ? "true" : "false",
                  kSupportedLanguagesJson);
    return buffer;
}

std::string make_wifi_scan_json_message(const wifi_manager::ScanResult* results, size_t count)
{
    std::string body;
    body.reserve(64u + (count * 64u));
    body += "{\"type\":\"wifi_scan\",\"networks\":[";
    for (size_t index = 0u; index < count; ++index) {
        if (index != 0u) {
            body += ',';
        }
        body += "{\"ssid\":\"" + json_escape(results[index].ssid) + "\"";
        body += ",\"rssi\":" + std::to_string(static_cast<int>(results[index].rssi));
        body += ",\"authmode\":" + std::to_string(static_cast<unsigned>(results[index].authmode));
        body += '}';
    }
    body += "]}";
    return body;
}

std::string make_system_info_json()
{
    const spi_link::BootProgress boot = spi_link::get_boot_progress();
    const spi_link::DefinitionsInfo defs = spi_link::get_definitions_info();
    const spi_link::SnapshotInfo snapshot = spi_link::get_snapshot_info();
    const wifi_manager::Status wifi = wifi_manager::get_status();
    const wifi_manager::SiteSettings settings = wifi_manager::get_site_settings();

    char buffer[1280] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "{\"boot\":{\"percent\":%u,\"loaded_points\":%u,\"total_points\":%u,\"link_ready\":%s,\"caps_received\":%s,\"snapshot_started\":%s,\"snapshot_complete\":%s},\"defs\":{\"generation\":%lu,\"point_count\":%u,\"loaded_bytes\":%lu,\"total_bytes\":%lu,\"complete\":%s},\"snapshot\":{\"sequence\":%lu,\"point_count\":%u,\"loaded_points\":%u,\"max_payload\":%u,\"complete\":%s},\"wifi\":{\"ap_started\":%s,\"sta_has_credentials\":%s,\"sta_connected\":%s,\"ap_ssid\":\"%s\",\"sta_ssid\":\"%s\",\"ap_ip\":\"%s\",\"sta_ip\":\"%s\"},\"settings\":{\"language\":\"%s\",\"anonymous\":%s,\"languages\":%s}}",
                  static_cast<unsigned>(boot.percent),
                  static_cast<unsigned>(boot.loaded_points),
                  static_cast<unsigned>(boot.total_points),
                  boot.link_ready ? "true" : "false",
                  boot.caps_received ? "true" : "false",
                  boot.snapshot_started ? "true" : "false",
                  boot.snapshot_complete ? "true" : "false",
                  static_cast<unsigned long>(defs.generation),
                  static_cast<unsigned>(defs.point_count),
                  static_cast<unsigned long>(defs.loaded_bytes),
                  static_cast<unsigned long>(defs.total_bytes),
                  defs.complete ? "true" : "false",
                  static_cast<unsigned long>(snapshot.sequence),
                  static_cast<unsigned>(snapshot.point_count),
                  static_cast<unsigned>(snapshot.loaded_points),
                  static_cast<unsigned>(snapshot.max_payload),
                  snapshot.complete ? "true" : "false",
                  wifi.ap_started ? "true" : "false",
                  wifi.sta_has_credentials ? "true" : "false",
                  wifi.sta_connected ? "true" : "false",
                  json_escape(wifi.ap_ssid).c_str(),
                  json_escape(wifi.sta_ssid).c_str(),
                  json_escape(wifi.ap_ip).c_str(),
                  json_escape(wifi.sta_ip).c_str(),
                  json_escape(settings.language).c_str(),
                  settings.anonymous_access ? "true" : "false",
                  kSupportedLanguagesJson);
    return buffer;
}

bool is_json_whitespace(char ch)
{
    return (ch == ' ') || (ch == '\t') || (ch == '\r') || (ch == '\n');
}

bool read_request_body(httpd_req_t* req, std::string* out_body)
{
    if ((req == nullptr) || (out_body == nullptr)) {
        return false;
    }

    std::vector<char> body(static_cast<size_t>(req->content_len) + 1u, '\0');
    int offset = 0;
    while (offset < req->content_len) {
        const int received = httpd_req_recv(req, body.data() + offset, req->content_len - offset);
        if (received <= 0) {
            return false;
        }
        offset += received;
    }

    *out_body = std::string(body.data());
    return true;
}

std::string extract_json_string(const std::string& payload, const char* key)
{
    const std::string quoted_key = std::string("\"") + key + "\"";
    const size_t key_pos = payload.find(quoted_key);
    if (key_pos == std::string::npos) {
        return std::string("\x01");
    }

    const size_t colon_pos = payload.find(':', key_pos + quoted_key.size());
    const size_t first_quote = payload.find('"', colon_pos + 1u);
    const size_t second_quote = payload.find('"', first_quote + 1u);
    if ((colon_pos == std::string::npos) || (first_quote == std::string::npos) || (second_quote == std::string::npos)) {
        return std::string("\x01");
    }

    return payload.substr(first_quote + 1u, second_quote - first_quote - 1u);
}

bool extract_json_bool(const std::string& payload, const char* key, bool* out_value)
{
    if (out_value == nullptr) {
        return false;
    }

    const std::string quoted_key = std::string("\"") + key + "\"";
    const size_t key_pos = payload.find(quoted_key);
    if (key_pos == std::string::npos) {
        return false;
    }

    const size_t colon_pos = payload.find(':', key_pos + quoted_key.size());
    if (colon_pos == std::string::npos) {
        return false;
    }

    size_t value_pos = colon_pos + 1u;
    while ((value_pos < payload.size()) && is_json_whitespace(payload[value_pos])) {
        ++value_pos;
    }

    if (payload.compare(value_pos, 4u, "true") == 0) {
        *out_value = true;
        return true;
    }
    if (payload.compare(value_pos, 5u, "false") == 0) {
        *out_value = false;
        return true;
    }

    return false;
}

bool is_startup_or_settings_uri(const char* uri)
{
    if (uri == nullptr) {
        return false;
    }

    const std::string path(uri);
    return (path == "/") ||
           (path.rfind("/startup", 0) == 0) ||
           (path.rfind("/api/wifi/", 0) == 0) ||
           (path == "/api/settings");
}

esp_err_t reject_if_anonymous_disabled(httpd_req_t* req)
{
    if ((req == nullptr) || wifi_manager::is_anonymous_access_allowed() || is_startup_or_settings_uri(req->uri)) {
        return ESP_OK;
    }

    return send_error(req, "403 Forbidden", "anonymous access disabled");
}

esp_err_t send_json(httpd_req_t* req, const std::string& body, const char* status = "200 OK")
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, status);
    return httpd_resp_send(req, body.c_str(), static_cast<ssize_t>(body.size()));
}

esp_err_t send_error(httpd_req_t* req, const char* status, const char* message)
{
    return send_json(req, std::string("{\"error\":\"") + json_escape(message) + "\"}", status);
}

void remove_ws_client(int fd)
{
    for (size_t index = 0; index < kMaxWsClients; ++index) {
        if (g_ws_clients[index] == fd) {
            g_ws_clients[index] = -1;
        }
    }
}

void add_ws_client(int fd)
{
    for (size_t index = 0; index < kMaxWsClients; ++index) {
        if (g_ws_clients[index] == fd) {
            return;
        }
    }

    for (size_t index = 0; index < kMaxWsClients; ++index) {
        if (g_ws_clients[index] < 0) {
            g_ws_clients[index] = fd;
            return;
        }
    }
}

void broadcast_text(const std::string& payload)
{
    if (g_server == nullptr) {
        return;
    }

    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(payload.c_str()));
    frame.len = payload.size();

    for (size_t index = 0; index < kMaxWsClients; ++index) {
        const int fd = g_ws_clients[index];
        if (fd < 0) {
            continue;
        }

        const esp_err_t err = httpd_ws_send_frame_async(g_server, fd, &frame);
        if (err != ESP_OK) {
            remove_ws_client(fd);
        }
    }
}

std::string guess_content_type(const std::string& path)
{
    if (path.ends_with(".html")) {
        return "text/html";
    }
    if (path.ends_with(".css")) {
        return "text/css";
    }
    if (path.ends_with(".js")) {
        return "application/javascript";
    }
    if (path.ends_with(".json")) {
        return "application/json";
    }
    return "text/plain";
}

std::string map_uri_to_file(const char* uri)
{
    std::string path = (uri != nullptr) ? uri : "/";
    if ((path.find("..") != std::string::npos) || (path.find("\\") != std::string::npos)) {
        return {};
    }

    const size_t query_pos = path.find_first_of("?#");
    if (query_pos != std::string::npos) {
        path.erase(query_pos);
    }

    if (path == "/startup") {
        path = "/startup/index.html";
    } else if (path == "/startup/") {
        path = "/startup/index.html";
    } else if (path == "/app") {
        path = "/app/index.html";
    } else if (path == "/app/") {
        path = "/app/index.html";
    }

    if ((path.rfind("/startup/", 0) != 0) && (path.rfind("/app/", 0) != 0) && (path.rfind("/vendor/", 0) != 0)) {
        return {};
    }

    return std::string(app_config::kWebMountPath) + path;
}

esp_err_t serve_file(httpd_req_t* req, const std::string& path)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return send_error(req, "404 Not Found", "asset not found");
    }

    httpd_resp_set_type(req, guess_content_type(path).c_str());
    char buffer[1024] = {};
    while (true) {
        const size_t read_count = std::fread(buffer, 1u, sizeof(buffer), file);
        if (read_count > 0u) {
            const esp_err_t err = httpd_resp_send_chunk(req, buffer, static_cast<ssize_t>(read_count));
            if (err != ESP_OK) {
                std::fclose(file);
                return err;
            }
        }

        if (read_count < sizeof(buffer)) {
            break;
        }
    }

    std::fclose(file);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t handle_root(httpd_req_t* req)
{
    const wifi_manager::Status wifi = wifi_manager::get_status();
    char host[64] = {};
    const size_t host_length = httpd_req_get_hdr_value_len(req, "Host");
    if ((host_length > 0u) && (host_length < sizeof(host)) &&
        (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) == ESP_OK)) {
        char* port = std::strchr(host, ':');
        if (port != nullptr) {
            *port = '\0';
        }
        if ((wifi.ap_ip[0] != '\0') && (std::strcmp(host, wifi.ap_ip) == 0)) {
            httpd_resp_set_status(req, "302 Found");
            httpd_resp_set_hdr(req, "Location", "/startup/");
            return httpd_resp_send(req, nullptr, 0);
        }
    }

    httpd_resp_set_status(req, "302 Found");
    const bool can_open_app = wifi_manager::has_saved_credentials() && wifi_manager::is_anonymous_access_allowed();
    httpd_resp_set_hdr(req, "Location", can_open_app ? "/app/" : "/startup/");
    return httpd_resp_send(req, nullptr, 0);
}

esp_err_t handle_static(httpd_req_t* req)
{
    ESP_RETURN_ON_ERROR(reject_if_anonymous_disabled(req), kLogTag, "anonymous access blocked");
    const std::string path = map_uri_to_file(req->uri);
    if (path.empty()) {
        return send_error(req, "404 Not Found", "unknown path");
    }
    return serve_file(req, path);
}

esp_err_t handle_system_info(httpd_req_t* req)
{
    ESP_RETURN_ON_ERROR(reject_if_anonymous_disabled(req), kLogTag, "anonymous access blocked");
    return send_json(req, make_system_info_json());
}

esp_err_t handle_wifi_status(httpd_req_t* req)
{
    return send_json(req, make_wifi_json_message(wifi_manager::get_status()));
}

esp_err_t handle_wifi_scan(httpd_req_t* req)
{
    wifi_manager::ScanResult results[16] = {};
    const size_t count = wifi_manager::scan_networks(results, sizeof(results) / sizeof(results[0]));
    return send_json(req, make_wifi_scan_json_message(results, count));
}

esp_err_t handle_settings_get(httpd_req_t* req)
{
    return send_json(req, make_settings_json_message(wifi_manager::get_site_settings()));
}

esp_err_t handle_wifi_config(httpd_req_t* req)
{
    std::string payload;
    if (!read_request_body(req, &payload)) {
        return send_error(req, "400 Bad Request", "request body truncated");
    }

    if (payload.find("\"ssid\"") == std::string::npos) {
        return send_error(req, "400 Bad Request", "ssid missing");
    }

    const std::string ssid = extract_json_string(payload, "ssid");
    std::string password;
    if (payload.find("\"password\"") != std::string::npos) {
        password = extract_json_string(payload, "password");
    }
    if ((ssid == std::string("\x01")) || ssid.empty()) {
        return send_error(req, "400 Bad Request", "invalid ssid");
    }
    if (password == std::string("\x01")) {
        return send_error(req, "400 Bad Request", "invalid password");
    }

    ESP_RETURN_ON_ERROR(wifi_manager::set_station_credentials(ssid.c_str(), password.c_str()),
                        kLogTag,
                        "set_station_credentials failed");
    return send_json(req, make_wifi_json_message(wifi_manager::get_status()));
}

esp_err_t handle_wifi_disconnect(httpd_req_t* req)
{
    ESP_RETURN_ON_ERROR(wifi_manager::disconnect_station(), kLogTag, "disconnect_station failed");
    return send_json(req, make_wifi_json_message(wifi_manager::get_status()));
}

esp_err_t handle_wifi_forget(httpd_req_t* req)
{
    ESP_RETURN_ON_ERROR(wifi_manager::forget_station_credentials(), kLogTag, "forget_station_credentials failed");
    return send_json(req, make_wifi_json_message(wifi_manager::get_status()));
}

esp_err_t handle_settings_post(httpd_req_t* req)
{
    std::string payload;
    if (!read_request_body(req, &payload)) {
        return send_error(req, "400 Bad Request", "request body truncated");
    }

    const std::string language = extract_json_string(payload, "language");
    bool anonymous_access = true;
    if ((language == std::string("\x01")) || !extract_json_bool(payload, "anonymous", &anonymous_access)) {
        return send_error(req, "400 Bad Request", "invalid settings payload");
    }

    const esp_err_t result = wifi_manager::set_site_settings(language.c_str(), anonymous_access);
    if (result == ESP_ERR_INVALID_ARG) {
        return send_error(req, "400 Bad Request", "unsupported language");
    }
    ESP_RETURN_ON_ERROR(result, kLogTag, "set_site_settings failed");
    return send_json(req, make_settings_json_message(wifi_manager::get_site_settings()));
}

esp_err_t handle_defs(httpd_req_t* req)
{
    ESP_RETURN_ON_ERROR(reject_if_anonymous_disabled(req), kLogTag, "anonymous access blocked");
    const spi_link::DefinitionsInfo defs = spi_link::get_definitions_info();
    std::vector<plclink::DefinitionRecordV1> records(defs.point_count);
    const size_t copied = spi_link::copy_definition_records(records.data(), records.size());

    std::string body;
    body.reserve(128u + (copied * 220u));
    body += "{\"generation\":" + std::to_string(static_cast<unsigned long>(defs.generation));
    body += ",\"point_count\":" + std::to_string(defs.point_count);
    body += ",\"loaded_bytes\":" + std::to_string(static_cast<unsigned long>(defs.loaded_bytes));
    body += ",\"total_bytes\":" + std::to_string(static_cast<unsigned long>(defs.total_bytes));
    body += ",\"complete\":" + std::string(defs.complete ? "true" : "false");
    body += ",\"records\":[";
    for (size_t index = 0u; index < copied; ++index) {
        const auto& record = records[index];
        const std::string path = build_point_path(record.device_id, record.feature, record.point_id);
        if (index != 0u) {
            body += ',';
        }
        body += "{\"point_index\":" + std::to_string(record.point_index);
        body += ",\"path\":\"" + json_escape(path.c_str()) + "\"";
        body += ",\"device_id\":\"" + json_escape(record.device_id) + "\"";
        body += ",\"feature\":\"" + json_escape(record.feature) + "\"";
        body += ",\"point_id\":\"" + json_escape(record.point_id) + "\"";
        body += ",\"display_name\":\"" + json_escape(record.display_name) + "\"";
        body += ",\"backend\":" + std::to_string(record.backend);
        body += ",\"direction\":" + std::to_string(record.direction);
        body += ",\"value_type\":" + std::to_string(record.value_type);
        body += ",\"string_capacity\":" + std::to_string(record.string_capacity);
        body += ",\"scale\":" + std::to_string(record.scale);
        body += ",\"unit\":\"" + json_escape(record.unit) + "\"}";
    }
    body += "]}";
    return send_json(req, body);
}

esp_err_t handle_snapshot(httpd_req_t* req)
{
    ESP_RETURN_ON_ERROR(reject_if_anonymous_disabled(req), kLogTag, "anonymous access blocked");
    const spi_link::SnapshotInfo snapshot = spi_link::get_snapshot_info();
    std::vector<spi_link::CachedStateRecord> records(snapshot.loaded_points);
    std::vector<plclink::DefinitionRecordV1> defs(snapshot.point_count);
    const size_t copied = spi_link::copy_cached_state_records(records.data(), records.size());
    const size_t defs_copied = spi_link::copy_definition_records(defs.data(), defs.size());

    std::string body;
    body.reserve(128u + (copied * 196u));
    body += "{\"sequence\":" + std::to_string(static_cast<unsigned long>(snapshot.sequence));
    body += ",\"point_count\":" + std::to_string(snapshot.point_count);
    body += ",\"loaded_points\":" + std::to_string(snapshot.loaded_points);
    body += ",\"complete\":" + std::string(snapshot.complete ? "true" : "false");
    body += ",\"records\":[";
    for (size_t index = 0; index < copied; ++index) {
        const auto& cached = records[index];
        const auto& record = cached.record;
        const plclink::DefinitionRecordV1* definition =
            (record.point_index < defs_copied) ? &defs[record.point_index] : nullptr;
        const std::string path = (definition != nullptr)
            ? build_point_path(definition->device_id, definition->feature, definition->point_id)
            : std::string();
        if (index != 0u) {
            body += ',';
        }
        body += "{\"point_index\":" + std::to_string(record.point_index);
        if (definition != nullptr) {
            body += ",\"path\":\"" + json_escape(path.c_str()) + "\"";
            body += ",\"device_id\":\"" + json_escape(definition->device_id) + "\"";
            body += ",\"feature\":\"" + json_escape(definition->feature) + "\"";
            body += ",\"point_id\":\"" + json_escape(definition->point_id) + "\"";
            body += ",\"display_name\":\"" + json_escape(definition->display_name) + "\"";
        }
        body += ",\"value_type\":" + std::to_string(record.value_type);
        body += ",\"state_flags\":" + std::to_string(record.state_flags);
        body += ",\"value_bits\":" + std::to_string(static_cast<unsigned long>(record.value_bits));
        body += ",\"quality\":" + std::to_string(static_cast<unsigned long>(record.quality));
        body += ",\"timestamp_ms\":" + std::to_string(static_cast<unsigned long>(record.timestamp_ms));
        if (record.value_type == kPointValueTypeString) {
            body += ",\"string_value\":\"" + json_escape(cached.string_value) + "\"";
        }
        body += '}';
    }
    body += "]}";
    return send_json(req, body);
}

esp_err_t handle_snapshot_refresh(httpd_req_t* req)
{
    ESP_RETURN_ON_ERROR(reject_if_anonymous_disabled(req), kLogTag, "anonymous access blocked");
    const esp_err_t result = spi_link::request_states_refresh();
    if ((result != ESP_OK) && (result != ESP_ERR_INVALID_STATE)) {
        return send_error(req, "409 Conflict", "snapshot refresh refused");
    }
    return send_json(req, "{\"accepted\":true}");
}

esp_err_t handle_ws(httpd_req_t* req)
{
    ESP_RETURN_ON_ERROR(reject_if_anonymous_disabled(req), kLogTag, "anonymous access blocked");
    if (req->method == HTTP_GET) {
        const int fd = httpd_req_to_sockfd(req);
        add_ws_client(fd);

        const std::string hello = std::string("{\"type\":\"hello\",\"system\":") + make_system_info_json() + '}';
        httpd_ws_frame_t frame = {};
        frame.type = HTTPD_WS_TYPE_TEXT;
        frame.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(hello.c_str()));
        frame.len = hello.size();
        return httpd_ws_send_frame(req, &frame);
    }

    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    ESP_RETURN_ON_ERROR(httpd_ws_recv_frame(req, &frame, 0), kLogTag, "ws recv header failed");
    std::vector<uint8_t> data(frame.len + 1u, 0u);
    frame.payload = data.data();
    ESP_RETURN_ON_ERROR(httpd_ws_recv_frame(req, &frame, frame.len), kLogTag, "ws recv payload failed");

    const std::string message(reinterpret_cast<char*>(data.data()), frame.len);
    if (message.find("refresh_states") != std::string::npos) {
        spi_link::request_states_refresh();
    }
    return ESP_OK;
}

} // namespace

esp_err_t start()
{
    if (g_server != nullptr) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(ensure_fs_mounted(), kLogTag, "web filesystem mount failed");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 16;

    ESP_RETURN_ON_ERROR(httpd_start(&g_server, &config), kLogTag, "httpd_start failed");

    const httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = handle_root,
        .user_ctx = nullptr,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };
    const httpd_uri_t system_info_uri = {
        .uri = "/api/system/info",
        .method = HTTP_GET,
        .handler = handle_system_info,
        .user_ctx = nullptr,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };
    const httpd_uri_t wifi_status_uri = {
        .uri = "/api/wifi/status",
        .method = HTTP_GET,
        .handler = handle_wifi_status,
        .user_ctx = nullptr,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };
    const httpd_uri_t wifi_scan_uri = {
        .uri = "/api/wifi/scan",
        .method = HTTP_GET,
        .handler = handle_wifi_scan,
        .user_ctx = nullptr,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };
    const httpd_uri_t settings_get_uri = {
        .uri = "/api/settings",
        .method = HTTP_GET,
        .handler = handle_settings_get,
        .user_ctx = nullptr,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };
    const httpd_uri_t wifi_config_uri = {
        .uri = "/api/wifi/config",
        .method = HTTP_POST,
        .handler = handle_wifi_config,
        .user_ctx = nullptr,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };
    const httpd_uri_t wifi_disconnect_uri = {
        .uri = "/api/wifi/disconnect",
        .method = HTTP_POST,
        .handler = handle_wifi_disconnect,
        .user_ctx = nullptr,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };
    const httpd_uri_t wifi_forget_uri = {
        .uri = "/api/wifi/forget",
        .method = HTTP_POST,
        .handler = handle_wifi_forget,
        .user_ctx = nullptr,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };
    const httpd_uri_t settings_post_uri = {
        .uri = "/api/settings",
        .method = HTTP_POST,
        .handler = handle_settings_post,
        .user_ctx = nullptr,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };
    const httpd_uri_t defs_uri = {
        .uri = "/api/plc/defs",
        .method = HTTP_GET,
        .handler = handle_defs,
        .user_ctx = nullptr,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };
    const httpd_uri_t snapshot_uri = {
        .uri = "/api/plc/states/snapshot",
        .method = HTTP_GET,
        .handler = handle_snapshot,
        .user_ctx = nullptr,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };
    const httpd_uri_t refresh_uri = {
        .uri = "/api/plc/states/refresh",
        .method = HTTP_POST,
        .handler = handle_snapshot_refresh,
        .user_ctx = nullptr,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };
    const httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = handle_ws,
        .user_ctx = nullptr,
        .is_websocket = true,
        .handle_ws_control_frames = true,
        .supported_subprotocol = nullptr,
    };
    const httpd_uri_t static_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = handle_static,
        .user_ctx = nullptr,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };

    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &root_uri), kLogTag, "register root failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &system_info_uri), kLogTag, "register system failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &wifi_status_uri), kLogTag, "register wifi status failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &wifi_scan_uri), kLogTag, "register wifi scan failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &settings_get_uri), kLogTag, "register settings get failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &wifi_config_uri), kLogTag, "register wifi config failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &wifi_disconnect_uri), kLogTag, "register wifi disconnect failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &wifi_forget_uri), kLogTag, "register wifi forget failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &settings_post_uri), kLogTag, "register settings post failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &defs_uri), kLogTag, "register defs failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &snapshot_uri), kLogTag, "register snapshot failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &refresh_uri), kLogTag, "register refresh failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &ws_uri), kLogTag, "register ws failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &static_uri), kLogTag, "register static failed");

    ESP_LOGI(kLogTag, "HTTP server started with LittleFS root %s", app_config::kWebMountPath);
    return ESP_OK;
}

void poll()
{
    if (g_server == nullptr) {
        return;
    }

    const spi_link::BootProgress boot = spi_link::get_boot_progress();
    const spi_link::SnapshotInfo snapshot = spi_link::get_snapshot_info();
    const wifi_manager::Status wifi = wifi_manager::get_status();
    const wifi_manager::SiteSettings settings = wifi_manager::get_site_settings();

    if (!g_have_last_boot || !boot_equal(boot, g_last_boot)) {
        g_last_boot = boot;
        g_have_last_boot = true;
        broadcast_text(make_boot_json(boot));
    }

    if (!g_have_last_snapshot || !snapshot_equal(snapshot, g_last_snapshot)) {
        g_last_snapshot = snapshot;
        g_have_last_snapshot = true;
        broadcast_text(make_snapshot_json(snapshot));
    }

    if (!g_have_last_wifi || !wifi_equal(wifi, g_last_wifi)) {
        g_last_wifi = wifi;
        g_have_last_wifi = true;
        broadcast_text(make_wifi_json_message(wifi));
    }

    if (!g_have_last_settings || !settings_equal(settings, g_last_settings)) {
        g_last_settings = settings;
        g_have_last_settings = true;
        broadcast_text(make_settings_json_message(settings));
    }

}

bool has_ws_clients()
{
    for (size_t index = 0; index < kMaxWsClients; ++index) {
        if (g_ws_clients[index] >= 0) {
            return true;
        }
    }
    return false;
}

} // namespace web_server
