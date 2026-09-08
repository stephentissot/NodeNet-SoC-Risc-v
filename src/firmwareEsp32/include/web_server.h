#pragma once

#include <esp_err.h>

namespace web_server {

esp_err_t start();
void poll();
bool has_ws_clients();

} // namespace web_server
