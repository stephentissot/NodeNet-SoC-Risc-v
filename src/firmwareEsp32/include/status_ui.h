#pragma once

#include <esp_err.h>

#include "wifi_manager.h"

namespace status_ui {

esp_err_t init();
void show();
void update(const char* language, const wifi_manager::Status& wifi);

} // namespace status_ui