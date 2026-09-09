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
#include <esp_random.h>
#include <sys/stat.h>

#include "app_config.h"
#include "spi_link.h"
#include "wifi_manager.h"

namespace web_server {
namespace {

constexpr const char* kLogTag = "web-server";
constexpr size_t kMaxWsClients = 4;
constexpr uint8_t kPointValueTypeString = 7u;
constexpr const char* kAuthCookieName = "nodenet_auth";
constexpr const char* kFakeAdminUsername = "admin";
constexpr const char* kFakeAdminPassword = "admin";
constexpr const char* kFakeAdminGroup = "admin";
constexpr const char* kFakeAdminPermission = "canAdmin";

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
char g_auth_token[33] = {};
bool g_auth_token_valid = false;

constexpr const char* kSupportedLanguagesJson = "[\"uk\",\"fr\",\"de\",\"es\",\"zh-Hans\"]";

struct FakeAuthState {
    bool anonymous_access = true;
    bool login_required = false;
    bool authenticated = false;
    bool can_admin = false;
    char username[16] = {};
    char group[16] = {};
};

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

std::string make_point_update_json(const spi_link::PointUpdate& update)
{
    const auto& record = update.state.record;
    const std::string path = update.has_definition
        ? build_point_path(update.definition.device_id, update.definition.feature, update.definition.point_id)
        : std::string();

    std::string body;
    body.reserve(384u);
    body += "{\"type\":\"plc_point_update\",\"snapshot\":{";
    body += "\"sequence\":" + std::to_string(static_cast<unsigned long>(update.sequence));
    body += ",\"point_count\":" + std::to_string(update.point_count);
    body += ",\"loaded_points\":" + std::to_string(update.loaded_points);
    body += ",\"complete\":" + std::string(update.complete ? "true" : "false");
    body += "},\"record\":{";
    body += "\"point_index\":" + std::to_string(record.point_index);
    if (update.has_definition) {
        body += ",\"path\":\"" + json_escape(path.c_str()) + "\"";
        body += ",\"device_id\":\"" + json_escape(update.definition.device_id) + "\"";
        body += ",\"feature\":\"" + json_escape(update.definition.feature) + "\"";
        body += ",\"point_id\":\"" + json_escape(update.definition.point_id) + "\"";
        body += ",\"display_name\":\"" + json_escape(update.definition.display_name) + "\"";
    }
    body += ",\"value_type\":" + std::to_string(record.value_type);
    body += ",\"state_flags\":" + std::to_string(record.state_flags);
    body += ",\"value_bits\":" + std::to_string(static_cast<unsigned long>(record.value_bits));
    body += ",\"quality\":" + std::to_string(static_cast<unsigned long>(record.quality));
    body += ",\"timestamp_ms\":" + std::to_string(static_cast<unsigned long>(record.timestamp_ms));
    if (record.value_type == kPointValueTypeString) {
        body += ",\"string_value\":\"" + json_escape(update.state.string_value) + "\"";
    }
    body += "}}";
    return body;
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

bool extract_json_u32(const std::string& payload, const char* key, uint32_t* out_value)
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
    if (value_pos >= payload.size()) {
        return false;
    }

    size_t end_pos = value_pos;
    while ((end_pos < payload.size()) && (payload[end_pos] >= '0') && (payload[end_pos] <= '9')) {
        ++end_pos;
    }
    if (end_pos == value_pos) {
        return false;
    }

    *out_value = static_cast<uint32_t>(std::strtoul(payload.substr(value_pos, end_pos - value_pos).c_str(), nullptr, 10));
    return true;
}

FakeAuthState make_anonymous_auth_state(bool anonymous_access)
{
    FakeAuthState state = {};
    state.anonymous_access = anonymous_access;
    state.login_required = !anonymous_access;
    state.authenticated = anonymous_access;
    state.can_admin = false;
    std::strncpy(state.username, anonymous_access ? "anonymous" : "", sizeof(state.username) - 1u);
    std::strncpy(state.group, anonymous_access ? "public" : "", sizeof(state.group) - 1u);
    return state;
}

FakeAuthState make_admin_auth_state(bool anonymous_access)
{
    FakeAuthState state = {};
    state.anonymous_access = anonymous_access;
    state.login_required = !anonymous_access;
    state.authenticated = true;
    state.can_admin = true;
    std::strncpy(state.username, kFakeAdminUsername, sizeof(state.username) - 1u);
    std::strncpy(state.group, kFakeAdminGroup, sizeof(state.group) - 1u);
    return state;
}

void clear_auth_token()
{
    g_auth_token[0] = '\0';
    g_auth_token_valid = false;
}

void generate_auth_token()
{
    const uint32_t token_a = esp_random();
    const uint32_t token_b = esp_random();
    std::snprintf(g_auth_token,
                  sizeof(g_auth_token),
                  "%08lx%08lx",
                  static_cast<unsigned long>(token_a),
                  static_cast<unsigned long>(token_b));
    g_auth_token_valid = true;
}

bool request_has_valid_auth_cookie(httpd_req_t* req)
{
    if ((req == nullptr) || !g_auth_token_valid || (g_auth_token[0] == '\0')) {
        return false;
    }

    const size_t cookie_length = httpd_req_get_hdr_value_len(req, "Cookie");
    if ((cookie_length == 0u) || (cookie_length > 255u)) {
        return false;
    }

    std::vector<char> cookie(cookie_length + 1u, '\0');
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie.data(), cookie.size()) != ESP_OK) {
        return false;
    }

    const std::string all_cookies(cookie.data());
    const std::string expected = std::string(kAuthCookieName) + '=' + g_auth_token;
    const size_t pos = all_cookies.find(expected);
    if (pos == std::string::npos) {
        return false;
    }

    if ((pos != 0u) && (all_cookies[pos - 1u] != ';') && (all_cookies[pos - 1u] != ' ')) {
        return false;
    }

    const size_t end = pos + expected.size();
    return (end == all_cookies.size()) || (all_cookies[end] == ';');
}

FakeAuthState get_auth_state_for_request(httpd_req_t* req)
{
    const bool anonymous_access = wifi_manager::is_anonymous_access_allowed();
    if (anonymous_access) {
        return make_anonymous_auth_state(true);
    }
    if (request_has_valid_auth_cookie(req)) {
        return make_admin_auth_state(false);
    }
    return make_anonymous_auth_state(false);
}

std::string make_auth_json_message(const FakeAuthState& auth)
{
    std::string body;
    body.reserve(240u);
    body += "{\"type\":\"auth\",\"auth\":{\"anonymous_access\":";
    body += auth.anonymous_access ? "true" : "false";
    body += ",\"login_required\":";
    body += auth.login_required ? "true" : "false";
    body += ",\"authenticated\":";
    body += auth.authenticated ? "true" : "false";
    body += ",\"can_admin\":";
    body += auth.can_admin ? "true" : "false";
    body += ",\"user\":";
    if (auth.username[0] == '\0') {
        body += "null";
    } else {
        body += "{\"username\":\"" + json_escape(auth.username) + "\",\"group\":\"" +
                json_escape(auth.group) + "\",\"permissions\":[";
        if (auth.can_admin) {
            body += "\"";
            body += kFakeAdminPermission;
            body += "\"";
        }
        body += "]}";
    }
    body += "}}";
    return body;
}

bool is_public_uri(const char* uri)
{
    if (uri == nullptr) {
        return false;
    }

    const std::string path(uri);
    return (path == "/") ||
           (path.rfind("/startup", 0) == 0) ||
           (path.rfind("/app", 0) == 0) ||
           (path.rfind("/vendor", 0) == 0) ||
           (path.rfind("/api/wifi/", 0) == 0) ||
           (path.rfind("/api/auth/", 0) == 0) ||
           (path == "/api/settings");
}

esp_err_t reject_if_unauthenticated(httpd_req_t* req)
{
    if ((req == nullptr) || is_public_uri(req->uri)) {
        return ESP_OK;
    }

    const FakeAuthState auth = get_auth_state_for_request(req);
    if (auth.authenticated) {
        return ESP_OK;
    }

    return send_error(req, "401 Unauthorized", "authentication required");
}

esp_err_t send_json(httpd_req_t* req, const std::string& body, const char* status = "200 OK")
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, status);
    return httpd_resp_send(req, body.c_str(), static_cast<ssize_t>(body.size()));
}

esp_err_t send_json_with_cookie(httpd_req_t* req,
                                const std::string& body,
                                const char* status,
                                const char* cookie_header)
{
    if ((req == nullptr) || (cookie_header == nullptr)) {
        return ESP_ERR_INVALID_ARG;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, status);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie_header);
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

bool is_ws_client_registered(int fd)
{
    for (size_t index = 0; index < kMaxWsClients; ++index) {
        if (g_ws_clients[index] == fd) {
            return true;
        }
    }

    return false;
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

const char* guess_content_type(const std::string& path)
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

    httpd_resp_set_type(req, guess_content_type(path));
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
    const bool can_open_app = wifi_manager::has_saved_credentials();
    httpd_resp_set_hdr(req, "Location", can_open_app ? "/app/" : "/startup/");
    return httpd_resp_send(req, nullptr, 0);
}

esp_err_t handle_static(httpd_req_t* req)
{
    ESP_RETURN_ON_ERROR(reject_if_unauthenticated(req), kLogTag, "request authentication failed");
    const std::string path = map_uri_to_file(req->uri);
    if (path.empty()) {
        return send_error(req, "404 Not Found", "unknown path");
    }
    return serve_file(req, path);
}

esp_err_t handle_system_info(httpd_req_t* req)
{
    ESP_RETURN_ON_ERROR(reject_if_unauthenticated(req), kLogTag, "request authentication failed");
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

esp_err_t handle_auth_status(httpd_req_t* req)
{
    return send_json(req, make_auth_json_message(get_auth_state_for_request(req)));
}

esp_err_t handle_auth_login(httpd_req_t* req)
{
    std::string payload;
    if (!read_request_body(req, &payload)) {
        return send_error(req, "400 Bad Request", "request body truncated");
    }

    const std::string username = extract_json_string(payload, "username");
    const std::string password = extract_json_string(payload, "password");
    if ((username == std::string("\x01")) || (password == std::string("\x01"))) {
        return send_error(req, "400 Bad Request", "invalid login payload");
    }

    if ((username != kFakeAdminUsername) || (password != kFakeAdminPassword)) {
        clear_auth_token();
        char cookie_header[96] = {};
        std::snprintf(cookie_header,
                      sizeof(cookie_header),
                      "%s=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax",
                      kAuthCookieName);
        return send_json_with_cookie(req,
                                     std::string("{\"error\":\"invalid username or password\"}"),
                                     "401 Unauthorized",
                                     cookie_header);
    }

    generate_auth_token();
    char cookie_header[128] = {};
    std::snprintf(cookie_header,
                  sizeof(cookie_header),
                  "%s=%s; Path=/; HttpOnly; SameSite=Lax",
                  kAuthCookieName,
                  g_auth_token);
    return send_json_with_cookie(req,
                                 make_auth_json_message(make_admin_auth_state(wifi_manager::is_anonymous_access_allowed())),
                                 "200 OK",
                                 cookie_header);
}

esp_err_t handle_auth_logout(httpd_req_t* req)
{
    clear_auth_token();
    char cookie_header[96] = {};
    std::snprintf(cookie_header,
                  sizeof(cookie_header),
                  "%s=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax",
                  kAuthCookieName);
    return send_json_with_cookie(req,
                                 make_auth_json_message(get_auth_state_for_request(req)),
                                 "200 OK",
                                 cookie_header);
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
    ESP_RETURN_ON_ERROR(reject_if_unauthenticated(req), kLogTag, "request authentication failed");
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
    ESP_RETURN_ON_ERROR(reject_if_unauthenticated(req), kLogTag, "request authentication failed");
    const spi_link::SnapshotInfo snapshot = spi_link::get_snapshot_info();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");

    std::string chunk;
    chunk.reserve(256u);
    chunk += "{\"sequence\":" + std::to_string(static_cast<unsigned long>(snapshot.sequence));
    chunk += ",\"point_count\":" + std::to_string(snapshot.point_count);
    chunk += ",\"loaded_points\":" + std::to_string(snapshot.loaded_points);
    chunk += ",\"complete\":" + std::string(snapshot.complete ? "true" : "false");
    chunk += ",\"records\":[";
    esp_err_t err = httpd_resp_sendstr_chunk(req, chunk.c_str());
    if (err != ESP_OK) {
        return err;
    }

    for (size_t index = 0u; index < snapshot.loaded_points; ++index) {
        spi_link::CachedStateRecord cached = {};
        if (!spi_link::copy_cached_state_record(index, &cached)) {
            break;
        }

        const auto& record = cached.record;
        plclink::DefinitionRecordV1 definition = {};
        const bool has_definition = spi_link::copy_definition_record(record.point_index, &definition);
        const std::string path = has_definition
            ? build_point_path(definition.device_id, definition.feature, definition.point_id)
            : std::string();

        chunk.clear();
        if (index != 0u) {
            chunk += ',';
        }
        chunk += "{\"point_index\":" + std::to_string(record.point_index);
        if (has_definition) {
            chunk += ",\"path\":\"" + json_escape(path.c_str()) + "\"";
            chunk += ",\"device_id\":\"" + json_escape(definition.device_id) + "\"";
            chunk += ",\"feature\":\"" + json_escape(definition.feature) + "\"";
            chunk += ",\"point_id\":\"" + json_escape(definition.point_id) + "\"";
            chunk += ",\"display_name\":\"" + json_escape(definition.display_name) + "\"";
        }
        chunk += ",\"value_type\":" + std::to_string(record.value_type);
        chunk += ",\"state_flags\":" + std::to_string(record.state_flags);
        chunk += ",\"value_bits\":" + std::to_string(static_cast<unsigned long>(record.value_bits));
        chunk += ",\"quality\":" + std::to_string(static_cast<unsigned long>(record.quality));
        chunk += ",\"timestamp_ms\":" + std::to_string(static_cast<unsigned long>(record.timestamp_ms));
        if (record.value_type == kPointValueTypeString) {
            chunk += ",\"string_value\":\"" + json_escape(cached.string_value) + "\"";
        }
        chunk += '}';

        err = httpd_resp_sendstr_chunk(req, chunk.c_str());
        if (err != ESP_OK) {
            return err;
        }
    }

    err = httpd_resp_sendstr_chunk(req, "]}");
    if (err != ESP_OK) {
        return err;
    }

    return httpd_resp_sendstr_chunk(req, nullptr);
}

esp_err_t handle_snapshot_refresh(httpd_req_t* req)
{
    ESP_RETURN_ON_ERROR(reject_if_unauthenticated(req), kLogTag, "request authentication failed");
    const esp_err_t result = spi_link::request_states_refresh();
    if ((result != ESP_OK) && (result != ESP_ERR_INVALID_STATE)) {
        return send_error(req, "409 Conflict", "snapshot refresh refused");
    }
    return send_json(req, "{\"accepted\":true}");
}

esp_err_t handle_plc_write(httpd_req_t* req)
{
    ESP_RETURN_ON_ERROR(reject_if_unauthenticated(req), kLogTag, "request authentication failed");

    std::string payload;
    if (!read_request_body(req, &payload)) {
        return send_error(req, "400 Bad Request", "request body truncated");
    }

    uint32_t point_index = 0u;
    uint32_t value_type = 0u;
    uint32_t value_bits = 0u;
    uint32_t write_flags = 1u;
    if (!extract_json_u32(payload, "point_index", &point_index) ||
        !extract_json_u32(payload, "value_type", &value_type) ||
        !extract_json_u32(payload, "value_bits", &value_bits)) {
        return send_error(req, "400 Bad Request", "invalid plc write payload");
    }
    (void)extract_json_u32(payload, "write_flags", &write_flags);

    const esp_err_t result = spi_link::request_write_state(static_cast<uint16_t>(point_index),
                                                           static_cast<uint8_t>(value_type),
                                                           static_cast<uint8_t>(write_flags),
                                                           value_bits);
    if (result != ESP_OK) {
        return send_error(req, "409 Conflict", "plc write refused");
    }

    return send_json(req, "{\"accepted\":true}");
}

esp_err_t handle_ws(httpd_req_t* req)
{
    if (req->method == HTTP_GET) {
        ESP_RETURN_ON_ERROR(reject_if_unauthenticated(req), kLogTag, "request authentication failed");
        const int fd = httpd_req_to_sockfd(req);
        add_ws_client(fd);

        const std::string hello = std::string("{\"type\":\"hello\",\"system\":") + make_system_info_json() + '}';
        httpd_ws_frame_t frame = {};
        frame.type = HTTPD_WS_TYPE_TEXT;
        frame.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(hello.c_str()));
        frame.len = hello.size();
        return httpd_ws_send_frame(req, &frame);
    }

    const int fd = httpd_req_to_sockfd(req);
    if (!is_ws_client_registered(fd)) {
        return send_error(req, "401 Unauthorized", "authentication required");
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
    config.max_uri_handlers = 20;

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
    const httpd_uri_t auth_status_uri = {
        .uri = "/api/auth/status",
        .method = HTTP_GET,
        .handler = handle_auth_status,
        .user_ctx = nullptr,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };
    const httpd_uri_t auth_login_uri = {
        .uri = "/api/auth/login",
        .method = HTTP_POST,
        .handler = handle_auth_login,
        .user_ctx = nullptr,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };
    const httpd_uri_t auth_logout_uri = {
        .uri = "/api/auth/logout",
        .method = HTTP_POST,
        .handler = handle_auth_logout,
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
    const httpd_uri_t plc_write_uri = {
        .uri = "/api/plc/write",
        .method = HTTP_POST,
        .handler = handle_plc_write,
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
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &auth_status_uri), kLogTag, "register auth status failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &auth_login_uri), kLogTag, "register auth login failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &auth_logout_uri), kLogTag, "register auth logout failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &wifi_config_uri), kLogTag, "register wifi config failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &wifi_disconnect_uri), kLogTag, "register wifi disconnect failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &wifi_forget_uri), kLogTag, "register wifi forget failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &settings_post_uri), kLogTag, "register settings post failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &defs_uri), kLogTag, "register defs failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &snapshot_uri), kLogTag, "register snapshot failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &refresh_uri), kLogTag, "register refresh failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(g_server, &plc_write_uri), kLogTag, "register plc write failed");
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

    for (size_t sent = 0u; sent < 16u; ++sent) {
        spi_link::PointUpdate update = {};
        if (!spi_link::pop_point_update(&update)) {
            break;
        }
        broadcast_text(make_point_update_json(update));
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
